package main

import (
	"crypto/sha256"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

type Agent struct {
	ID          string    `json:"agent_id"`
	Hostname    string    `json:"hostname"`
	OS          string    `json:"os"`
	User        string    `json:"user"`
	IP          string    `json:"ip"`
	LastSeen    time.Time `json:"last_seen"`
	ReportCount int       `json:"report_count"`
}
type Report struct {
	AgentID   string          `json:"agent_id"`
	Timestamp time.Time       `json:"timestamp"`
	Hostname  string          `json:"hostname"`
	OS        string          `json:"os"`
	User      string          `json:"user"`
	Report    json.RawMessage `json:"report"`
}
type credentials struct {
	ID    string `json:"id"`
	Token string `json:"token"`
}
type service struct {
	mu            sync.RWMutex
	agents        map[string]*Agent
	reports       map[string][]Report
	tokens        map[string][32]byte
	operatorKey   [32]byte
	retention     int
	allowedOrigin string
	logger        *log.Logger
}

const maxBody = 64 << 20

func newService(seeds []credentials, operatorKey string, retention int, origin string, logger *log.Logger) (*service, error) {
	if operatorKey == "" || retention < 1 || len(seeds) == 0 {
		return nil, errors.New("operator key, agents and positive retention are required")
	}
	s := &service{agents: make(map[string]*Agent), reports: make(map[string][]Report), tokens: make(map[string][32]byte), operatorKey: sha256.Sum256([]byte(operatorKey)), retention: retention, allowedOrigin: origin, logger: logger}
	for _, seed := range seeds {
		if seed.ID == "" || seed.Token == "" {
			return nil, errors.New("agent IDs and tokens must be nonempty")
		}
		if _, exists := s.tokens[seed.ID]; exists {
			return nil, fmt.Errorf("duplicate agent ID %q", seed.ID)
		}
		hash := sha256.Sum256([]byte(seed.Token))
		for _, prev := range s.tokens {
			if subtle.ConstantTimeCompare(hash[:], prev[:]) == 1 {
				return nil, errors.New("agent tokens must be unique")
			}
		}
		s.tokens[seed.ID] = hash
		s.agents[seed.ID] = &Agent{ID: seed.ID}
	}
	return s, nil
}
func (s *service) event(fields map[string]any) {
	data, err := json.Marshal(fields)
	if err == nil {
		s.logger.Print(string(data))
	}
}
func (s *service) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/agent/report", s.receive)
	mux.HandleFunc("/api/op/agents", s.authOperator(s.listAgents))
	mux.HandleFunc("/api/op/reports", s.authOperator(s.listReports))
	mux.HandleFunc("/api/op/reports/latest", s.authOperator(s.latestReport))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		origin := r.Header.Get("Origin")
		if origin != "" && origin == s.allowedOrigin {
			w.Header().Set("Access-Control-Allow-Origin", origin)
			w.Header().Set("Vary", "Origin")
			w.Header().Set("Access-Control-Allow-Headers", "Content-Type, X-Operator-Key")
			w.Header().Set("Access-Control-Allow-Methods", "GET, OPTIONS")
		}
		if r.Method == http.MethodOptions && strings.HasPrefix(r.URL.Path, "/api/op/") {
			if origin == "" || origin != s.allowedOrigin {
				http.Error(w, "origin not allowed", http.StatusForbidden)
				return
			}
			w.WriteHeader(http.StatusNoContent)
			return
		}
		mux.ServeHTTP(w, r)
	})
}
func only(w http.ResponseWriter, r *http.Request, method string) bool {
	if r.Method == method {
		return true
	}
	w.Header().Set("Allow", method)
	http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	return false
}
func respond(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
func (s *service) authOperator(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		candidate := sha256.Sum256([]byte(r.Header.Get("X-Operator-Key")))
		if subtle.ConstantTimeCompare(candidate[:], s.operatorKey[:]) != 1 {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		next(w, r)
	}
}
func (s *service) receive(w http.ResponseWriter, r *http.Request) {
	if !only(w, r, http.MethodPost) {
		return
	}
	// Authenticate before reading a potentially large report.
	header := r.Header.Get("Authorization")
	if !strings.HasPrefix(header, "Bearer ") {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	hash := sha256.Sum256([]byte(strings.TrimPrefix(header, "Bearer ")))
	agentID := ""
	for id, expected := range s.tokens {
		if subtle.ConstantTimeCompare(hash[:], expected[:]) == 1 {
			agentID = id
		}
	}
	if agentID == "" {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	data, err := io.ReadAll(http.MaxBytesReader(w, r.Body, maxBody))
	if err != nil {
		http.Error(w, "report too large or unreadable", http.StatusRequestEntityTooLarge)
		return
	}
	var input struct {
		AgentID  string          `json:"agent_id"`
		Hostname string          `json:"hostname"`
		OS       string          `json:"os"`
		User     string          `json:"user"`
		Report   json.RawMessage `json:"report"`
	}
	if json.Unmarshal(data, &input) != nil || input.AgentID != agentID {
		http.Error(w, "invalid report or agent ID", http.StatusBadRequest)
		return
	}
	var collectors map[string]json.RawMessage
	if json.Unmarshal(input.Report, &collectors) != nil || collectors == nil {
		http.Error(w, "report must be an object", http.StatusBadRequest)
		return
	}
	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		ip = r.RemoteAddr
	}
	s.mu.Lock()
	now := time.Now().UTC()
	report := Report{AgentID: agentID, Timestamp: now, Hostname: input.Hostname, OS: input.OS, User: input.User, Report: input.Report}
	a := s.agents[agentID]
	a.Hostname = input.Hostname
	a.OS = input.OS
	a.User = input.User
	a.IP = ip
	a.LastSeen = now
	records := append(s.reports[agentID], report)
	if len(records) > s.retention {
		records = append([]Report(nil), records[len(records)-s.retention:]...)
	}
	s.reports[agentID] = records
	a.ReportCount = len(records)
	s.mu.Unlock()
	count := 0
	for _, key := range []string{"sys.info", "proc.list", "net.conn", "svc.list", "persist.check", "software.list", "fs.list"} {
		if _, found := collectors[key]; found {
			count++
		}
	}
	s.event(map[string]any{"event": "report_received", "agent_id": agentID, "ip": ip, "size": len(data), "collector_count": count})
	w.WriteHeader(http.StatusNoContent)
}
func (s *service) listAgents(w http.ResponseWriter, r *http.Request) {
	if !only(w, r, http.MethodGet) {
		return
	}
	s.mu.RLock()
	agents := make([]Agent, 0, len(s.agents))
	for _, a := range s.agents {
		agents = append(agents, *a)
	}
	s.mu.RUnlock()
	sort.Slice(agents, func(i, j int) bool { return agents[i].ID < agents[j].ID })
	s.event(map[string]any{"event": "operator_read", "agent_id": "", "endpoint": r.URL.Path})
	respond(w, http.StatusOK, agents)
}
func (s *service) listReports(w http.ResponseWriter, r *http.Request) {
	if !only(w, r, http.MethodGet) {
		return
	}
	limit, offset := 50, 0
	var err error
	if x := r.URL.Query().Get("limit"); x != "" {
		limit, err = strconv.Atoi(x)
		if err != nil || limit < 1 || limit > 100 {
			http.Error(w, "limit must be 1..100", http.StatusBadRequest)
			return
		}
	}
	if x := r.URL.Query().Get("offset"); x != "" {
		offset, err = strconv.Atoi(x)
		if err != nil || offset < 0 {
			http.Error(w, "offset must be nonnegative", http.StatusBadRequest)
			return
		}
	}
	id := r.URL.Query().Get("agent_id")
	s.mu.RLock()
	all := []Report{}
	if id != "" {
		all = append(all, s.reports[id]...)
	} else {
		for _, records := range s.reports {
			all = append(all, records...)
		}
	}
	s.mu.RUnlock()
	sort.SliceStable(all, func(i, j int) bool { return all[i].Timestamp.After(all[j].Timestamp) })
	total := len(all)
	if offset > total {
		offset = total
	}
	end := offset + limit
	if end > total {
		end = total
	}
	s.event(map[string]any{"event": "operator_read", "agent_id": id, "endpoint": r.URL.Path})
	respond(w, http.StatusOK, map[string]any{"reports": all[offset:end], "total": total, "limit": limit, "offset": offset})
}
func (s *service) latestReport(w http.ResponseWriter, r *http.Request) {
	if !only(w, r, http.MethodGet) {
		return
	}
	id := r.URL.Query().Get("agent_id")
	if id == "" {
		http.Error(w, "agent_id required", http.StatusBadRequest)
		return
	}
	s.mu.RLock()
	records := s.reports[id]
	var report Report
	found := len(records) > 0
	if found {
		report = records[len(records)-1]
	}
	s.mu.RUnlock()
	s.event(map[string]any{"event": "operator_read", "agent_id": id, "endpoint": r.URL.Path})
	if !found {
		http.Error(w, "no report found", http.StatusNotFound)
		return
	}
	respond(w, http.StatusOK, report)
}
func run() error {
	file := os.Getenv("AGENTS_FILE")
	if file == "" {
		file = "agents.json"
	}
	raw, err := os.ReadFile(file)
	if err != nil {
		return err
	}
	var seeds []credentials
	if err := json.Unmarshal(raw, &seeds); err != nil {
		return err
	}
	retention := 100
	if v := os.Getenv("REPORT_RETENTION"); v != "" {
		retention, err = strconv.Atoi(v)
		if err != nil {
			return errors.New("invalid REPORT_RETENTION")
		}
	}
	logger := log.New(os.Stdout, "", 0)
	s, err := newService(seeds, os.Getenv("OP_KEY"), retention, os.Getenv("DASHBOARD_ORIGIN"), logger)
	if err != nil {
		return err
	}
	cert, key := os.Getenv("TLS_CERT"), os.Getenv("TLS_KEY")
	if cert == "" || key == "" {
		return errors.New("TLS_CERT and TLS_KEY are required")
	}
	server := &http.Server{Addr: ":8443", Handler: s.routes(), ReadHeaderTimeout: 10 * time.Second, ReadTimeout: 2 * time.Minute, WriteTimeout: 2 * time.Minute, IdleTimeout: 60 * time.Second, MaxHeaderBytes: 16 << 10}
	return server.ListenAndServeTLS(cert, key)
}
func main() {
	if err := run(); err != nil {
		log.New(os.Stderr, "", 0).Fatal(err)
	}
}

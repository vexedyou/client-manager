package main

import (
	"bytes"
	"encoding/json"
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
)

func fixture(t *testing.T, retention int) *service {
	t.Helper()
	s, err := newService([]credentials{{ID: "one", Token: "secret-one"}, {ID: "two", Token: "secret-two"}}, "op-secret", retention, "https://dashboard.test", log.New(io.Discard, "", 0))
	if err != nil {
		t.Fatal(err)
	}
	return s
}
func call(s *service, method, path, token, body string) *httptest.ResponseRecorder {
	r := httptest.NewRequest(method, path, strings.NewReader(body))
	r.RemoteAddr = "192.0.2.8:1234"
	if strings.HasPrefix(path, "/api/agent/") {
		r.Header.Set("Authorization", "Bearer "+token)
	} else {
		r.Header.Set("X-Operator-Key", token)
	}
	w := httptest.NewRecorder()
	s.routes().ServeHTTP(w, r)
	return w
}
func TestUploadAuthRetentionAndRead(t *testing.T) {
	s := fixture(t, 2)
	if w := call(s, "POST", "/api/agent/report", "wrong", `{"agent_id":"one","report":{}}`); w.Code != 401 {
		t.Fatalf("bad token: %d", w.Code)
	}
	if w := call(s, "POST", "/api/agent/report", "secret-one", `{"agent_id":"two","report":{}}`); w.Code != 400 {
		t.Fatalf("ID mismatch: %d", w.Code)
	}
	if w := call(s, "POST", "/api/agent/report", "secret-one", `{"agent_id":"one","report":null}`); w.Code != 400 {
		t.Fatalf("null report: %d", w.Code)
	}
	for _, body := range []string{`{"agent_id":"one","hostname":"host","os":"Win11","user":"user","report":{"version":1}}`, `{"agent_id":"one","hostname":"host","report":{"version":2}}`, `{"agent_id":"one","hostname":"host","report":{"version":3}}`} {
		if w := call(s, "POST", "/api/agent/report", "secret-one", body); w.Code != 204 {
			t.Fatalf("upload %d %s", w.Code, w.Body.String())
		}
	}
	if w := call(s, "GET", "/api/op/agents", "wrong", ""); w.Code != 401 {
		t.Fatalf("operator auth: %d", w.Code)
	}
	var agents []Agent
	w := call(s, "GET", "/api/op/agents", "op-secret", "")
	if err := json.Unmarshal(w.Body.Bytes(), &agents); err != nil {
		t.Fatal(err)
	}
	if len(agents) != 2 || agents[0].ReportCount != 2 || agents[0].IP != "192.0.2.8" {
		t.Fatalf("agents: %+v", agents)
	}
	w = call(s, "GET", "/api/op/reports?agent_id=one&limit=1&offset=1", "op-secret", "")
	var page struct {
		Reports []Report `json:"reports"`
		Total   int      `json:"total"`
	}
	if err := json.Unmarshal(w.Body.Bytes(), &page); err != nil {
		t.Fatal(err)
	}
	if page.Total != 2 || len(page.Reports) != 1 || !bytes.Contains(page.Reports[0].Report, []byte(`"version":2`)) {
		t.Fatalf("page: %s", w.Body.String())
	}
	w = call(s, "GET", "/api/op/reports/latest?agent_id=one", "op-secret", "")
	if w.Code != 200 || !strings.Contains(w.Body.String(), `"version":3`) {
		t.Fatalf("latest: %d %s", w.Code, w.Body.String())
	}
	if w = call(s, "GET", "/api/op/reports?limit=0", "op-secret", ""); w.Code != 400 {
		t.Fatalf("limit: %d", w.Code)
	}
	if w = call(s, "GET", "/api/op/reports/latest?agent_id=two", "op-secret", ""); w.Code != 404 {
		t.Fatalf("empty: %d", w.Code)
	}
}
func TestConcurrentUploadsAndReads(t *testing.T) {
	s := fixture(t, 10)
	var wg sync.WaitGroup
	for i := 0; i < 20; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			w := call(s, "POST", "/api/agent/report", "secret-one", `{"agent_id":"one","report":{"ok":true}}`)
			if w.Code != http.StatusNoContent {
				t.Errorf("upload: %d", w.Code)
			}
			_ = call(s, "GET", "/api/op/reports", "op-secret", "")
		}()
	}
	wg.Wait()
	s.mu.RLock()
	defer s.mu.RUnlock()
	if len(s.reports["one"]) != 10 {
		t.Fatalf("retention: %d", len(s.reports["one"]))
	}
}
func TestOriginAndDuplicateCredentials(t *testing.T) {
	s := fixture(t, 1)
	r := httptest.NewRequest("OPTIONS", "/api/op/agents", nil)
	r.Header.Set("Origin", "https://dashboard.test")
	w := httptest.NewRecorder()
	s.routes().ServeHTTP(w, r)
	if w.Code != 204 || w.Header().Get("Access-Control-Allow-Origin") != "https://dashboard.test" {
		t.Fatalf("preflight: %d", w.Code)
	}
	_, err := newService([]credentials{{ID: "a", Token: "same"}, {ID: "b", Token: "same"}}, "key", 1, "", log.New(io.Discard, "", 0))
	if err == nil {
		t.Fatal("duplicate token accepted")
	}
}

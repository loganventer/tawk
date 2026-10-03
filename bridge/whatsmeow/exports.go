// Package main is built with -buildmode=c-archive and linked into the tawk
// binary. The C side talks to it with the same JSON line protocol the Node.js
// bridge uses: commands go in through TawkWmCommand, events come back
// through the C function tawk_wm_emit. Several sessions, one for each
// account, live side by side; the C side names each with a handle of its
// choosing, and every call and every event carries it.
package main

/*
#include <stdlib.h>
extern void tawk_wm_emit(int handle, char *line);
*/
import "C"

import (
	"encoding/json"
	"sync"
	"unsafe"
)

var (
	sessionsMu sync.Mutex
	sessions   = map[int]*Session{}
)

// emitTo sends one protocol event to the C side for the session `handle`.
func emitTo(handle int, event map[string]any) {
	line, err := json.Marshal(event)
	if err != nil {
		return
	}
	cs := C.CString(string(line))
	C.tawk_wm_emit(C.int(handle), cs)
	C.free(unsafe.Pointer(cs))
}

func sessionOf(handle int) *Session {
	sessionsMu.Lock()
	defer sessionsMu.Unlock()
	return sessions[handle]
}

//export TawkWmInit
func TawkWmInit(handle C.int, config *C.char) C.int {
	var cfg Config
	if err := json.Unmarshal([]byte(C.GoString(config)), &cfg); err != nil {
		return -1
	}
	h := int(handle)
	sessionsMu.Lock()
	defer sessionsMu.Unlock()
	if sessions[h] != nil {
		return 0
	}
	s, err := NewSession(h, cfg)
	if err != nil {
		emitTo(h, map[string]any{"evt": "error", "detail": "Could not open the login store: " + err.Error()})
		return -1
	}
	sessions[h] = s
	return 0
}

//export TawkWmCommand
func TawkWmCommand(handle C.int, line *C.char) C.int {
	var cmd Command
	if err := json.Unmarshal([]byte(C.GoString(line)), &cmd); err != nil {
		return -1
	}
	s := sessionOf(int(handle))
	if s == nil || !s.Enqueue(cmd) {
		return -1
	}
	return 0
}

//export TawkWmShutdown
func TawkWmShutdown(handle C.int) {
	h := int(handle)
	sessionsMu.Lock()
	s := sessions[h]
	delete(sessions, h)
	sessionsMu.Unlock()
	if s != nil {
		s.Close()
	}
}

func main() {}

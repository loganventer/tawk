package main

import (
	"context"
	"database/sql"
	"path/filepath"

	"go.mau.fi/whatsmeow/store"
	"go.mau.fi/whatsmeow/types"
)

// WhatsApp addresses a person either by phone number (…@s.whatsapp.net) or by
// an anonymous LID (…@lid). tawk keys chats by phone number, so every known
// LID to phone mapping is reported as an "alias" event and the C side merges
// anything it had stored under the LID.

// emitAlias reports lid -> pn when both halves have the expected servers.
func (s *Session) emitAlias(lid, pn types.JID) {
	if lid.Server != types.HiddenUserServer || pn.Server != types.DefaultUserServer || lid.User == "" || pn.User == "" {
		return
	}
	s.emit(map[string]any{"evt": "alias", "lid": lid.ToNonAD().String(), "pn": pn.ToNonAD().String()})
}

// rememberAlias stores a mapping learnt from message metadata so later
// lookups (contacts, push names) resolve too, then reports it.
func (s *Session) rememberAlias(lid, pn types.JID) {
	if lid.Server != types.HiddenUserServer || pn.Server != types.DefaultUserServer {
		return
	}
	if s.client != nil {
		_ = s.client.Store.LIDs.PutLIDMapping(context.Background(), lid.ToNonAD(), pn.ToNonAD())
	}
	s.emitAlias(lid, pn)
}

// emitStoredAliases reports every mapping whatsmeow has persisted, which
// heals chats that were stored under a LID before the mapping was known.
func (s *Session) emitStoredAliases() {
	db, err := sql.Open("sqlite3", "file:"+filepath.Join(s.cfg.AuthDir, "whatsmeow.db")+"?mode=ro&_pragma=busy_timeout(5000)")
	if err != nil {
		return
	}
	defer db.Close()
	rows, err := db.Query("SELECT lid, pn FROM whatsmeow_lid_map")
	if err != nil {
		return
	}
	defer rows.Close()
	for rows.Next() {
		var lid, pn string
		if rows.Scan(&lid, &pn) == nil {
			s.emitAlias(types.NewJID(lid, types.HiddenUserServer), types.NewJID(pn, types.DefaultUserServer))
		}
	}
}

// storeHistoryMappings saves the mappings that arrive with a history sync.
func (s *Session) storeHistoryMappings(pairs [][2]types.JID) {
	if s.client == nil || len(pairs) == 0 {
		return
	}
	mappings := make([]store.LIDMapping, 0, len(pairs))
	for _, p := range pairs {
		mappings = append(mappings, store.LIDMapping{LID: p[0], PN: p[1]})
		s.emitAlias(p[0], p[1])
	}
	_ = s.client.Store.LIDs.PutManyLIDMappings(context.Background(), mappings)
}

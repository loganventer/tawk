package main

import (
	"context"
	"time"

	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
)

// Calls: tawk cannot carry call audio (no library implements WhatsApp's
// call media), so it only reports calls and can decline them.

func (s *Session) emitCallOffer(evt *events.CallOffer) {
	video := false
	if evt.Data != nil {
		_, video = evt.Data.GetOptionalChildByTag("video")
	}
	s.emitCall(evt.BasicCallMeta, "offer", video, false)
}

func (s *Session) emitCall(meta types.BasicCallMeta, state string, video, group bool) {
	if !isSafeID(meta.CallID) {
		return
	}
	s.emit(map[string]any{"evt": "call", "id": meta.CallID, "from": s.phoneJID(meta.From), "state": state,
		"video": video, "group": group, "ts": meta.Timestamp.Unix()})
}

// rejectCall declines an incoming call.
func (s *Session) rejectCall(cmd Command) {
	if s.client == nil || !s.client.IsLoggedIn() || !isSafeID(cmd.ID) {
		return
	}
	from, err := types.ParseJID(cmd.JID)
	if err != nil {
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 15*time.Second)
	defer cancel()
	_ = s.client.RejectCall(ctx, from, cmd.ID)
}

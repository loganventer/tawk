package main

import (
	"context"
	"errors"
	"os"
	"time"

	"go.mau.fi/whatsmeow/appstate"
	"go.mau.fi/whatsmeow/types"
)

var errNotConnected = errors.New("not connected to WhatsApp")

// profileUpdated reports how a change to your own profile went.
func (s *Session) profileUpdated(field string, err error, extra map[string]any) {
	out := map[string]any{"evt": "profile_updated", "field": field, "ok": err == nil}
	if err != nil {
		out["detail"] = err.Error()
	}
	for k, v := range extra {
		out[k] = v
	}
	s.emit(out)
}

// ownJID is the linked account's phone-number JID, "" before linking.
func (s *Session) ownJID() string {
	if s.client == nil || s.client.Store.ID == nil {
		return ""
	}
	return s.client.Store.ID.ToNonAD().String()
}

func (s *Session) accountReady(field string) bool {
	if s.client == nil || !s.client.IsLoggedIn() {
		s.profileUpdated(field, errNotConnected, nil)
		return false
	}
	return true
}

// setName changes the name your contacts see (the push name). WhatsApp keeps
// it in the app state, so it goes out as an app state patch.
func (s *Session) setName(cmd Command) {
	if !s.accountReady("name") {
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 30*time.Second)
	defer cancel()
	if err := s.client.SendAppState(ctx, appstate.BuildSettingPushName(cmd.PushName)); err != nil {
		s.profileUpdated("name", err, nil)
		return
	}
	s.client.Store.PushName = cmd.PushName
	_ = s.client.Store.Save(ctx)
	s.profileUpdated("name", nil, map[string]any{"name": cmd.PushName})
}

// setAbout changes the about text on your profile.
func (s *Session) setAbout(cmd Command) {
	if !s.accountReady("about") {
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 30*time.Second)
	defer cancel()
	text := cmd.Text
	if err := s.client.SetStatusMessage(ctx, types.SetStatusInput{Text: &text}); err != nil {
		s.profileUpdated("about", err, nil)
		return
	}
	s.profileUpdated("about", nil, nil)
	s.profile(Command{JID: s.ownJID()})
}

// setPicture makes a JPEG or PNG from the media folder your profile photo.
// SetGroupPhoto with no JID leaves out the target, which is the request the
// official clients send for their own picture.
func (s *Session) setPicture(cmd Command) {
	if !s.accountReady("picture") {
		return
	}
	if !s.insideMediaDir(cmd.Path) {
		s.profileUpdated("picture", errMediaFile, nil)
		return
	}
	f, err := os.Open(cmd.Path)
	if err != nil {
		s.profileUpdated("picture", errMediaFile, nil)
		return
	}
	jpeg, err := avatarJPEG(f)
	f.Close()
	if err != nil {
		s.profileUpdated("picture", err, nil)
		return
	}
	s.changePicture(jpeg)
}

// removePicture clears your profile photo.
func (s *Session) removePicture() {
	if !s.accountReady("picture") {
		return
	}
	s.changePicture(nil)
}

func (s *Session) changePicture(jpeg []byte) {
	ctx, cancel := context.WithTimeout(s.ctx, 60*time.Second)
	defer cancel()
	if _, err := s.client.SetGroupPhoto(ctx, types.EmptyJID, jpeg); err != nil {
		s.profileUpdated("picture", err, nil)
		return
	}
	s.profileUpdated("picture", nil, nil)
	if jid := s.ownJID(); jid != "" {
		s.emit(map[string]any{"evt": "picture_changed", "jid": jid})
	}
}

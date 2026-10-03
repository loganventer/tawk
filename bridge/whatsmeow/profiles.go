package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
)

const maxPictureBytes = 4 << 20

// profile publishes what WhatsApp tells a linked device about a contact or
// group: the about text, business details, and for groups the description,
// owner, creation date and members.
func (s *Session) profile(cmd Command) {
	if s.client == nil || !s.client.IsLoggedIn() {
		return
	}
	jid, err := types.ParseJID(cmd.JID)
	if err != nil {
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 30*time.Second)
	defer cancel()
	out := map[string]any{"evt": "profile", "jid": cmd.JID}
	if jid.Server == types.GroupServer {
		if info, err := s.client.GetGroupInfo(ctx, jid); err == nil {
			group := map[string]any{
				"subject":     info.Name,
				"description": info.Topic,
				"owner":       s.phoneJID(info.OwnerJID),
				"created":     info.GroupCreated.Unix(),
			}
			var members []map[string]any
			for _, p := range info.Participants {
				members = append(members, map[string]any{"jid": s.phoneJID(p.JID), "admin": p.IsAdmin || p.IsSuperAdmin})
			}
			group["participants"] = members
			out["group"] = group
		}
	} else {
		if infos, err := s.client.GetUserInfo(ctx, []types.JID{jid}); err == nil {
			if info, ok := infos[jid]; ok {
				out["about"] = info.Status
				if info.VerifiedName != nil && info.VerifiedName.Details != nil {
					out["verified_name"] = info.VerifiedName.Details.GetVerifiedName()
				}
			}
		}
		if biz, err := s.client.GetBusinessProfile(ctx, jid); err == nil && biz != nil {
			var categories []string
			for _, c := range biz.Categories {
				categories = append(categories, c.Name)
			}
			out["business"] = map[string]any{"address": biz.Address, "email": biz.Email, "category": strings.Join(categories, ", ")}
		}
	}
	s.emit(out)
}

// picture downloads a contact's or group's profile picture (the small
// preview, or the full picture when cmd.Full is set) into the media folder.
func (s *Session) picture(cmd Command) {
	if s.client == nil || !s.client.IsLoggedIn() {
		return
	}
	jid, err := types.ParseJID(cmd.JID)
	if err != nil {
		return
	}
	full := cmd.Full
	ctx, cancel := context.WithTimeout(s.ctx, 30*time.Second)
	defer cancel()
	info, err := s.client.GetProfilePictureInfo(ctx, jid, &whatsmeow.GetProfilePictureParams{Preview: !full})
	if err != nil || info == nil || info.URL == "" {
		s.emit(map[string]any{"evt": "picture", "jid": cmd.JID, "full": full, "none": true})
		return
	}
	sum := sha256.Sum256([]byte(cmd.JID))
	name := "pic-" + hex.EncodeToString(sum[:8]) + "-" + safeName(info.ID)
	if full {
		name += "-full"
	}
	path := filepath.Join(s.cfg.MediaDir, name+".jpg")
	if _, err := os.Stat(path); err != nil {
		if err := download(ctx, info.URL, path); err != nil {
			return
		}
	}
	s.emit(map[string]any{"evt": "picture", "jid": cmd.JID, "full": full, "path": path, "id": info.ID})
}

func safeName(s string) string {
	var b strings.Builder
	for _, r := range s {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') {
			b.WriteRune(r)
		}
	}
	if b.Len() == 0 {
		return "0"
	}
	return b.String()
}

// download fetches an https URL into path (at most maxPictureBytes).
func download(ctx context.Context, url, path string) error {
	if !strings.HasPrefix(url, "https://") {
		return os.ErrInvalid
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return err
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return os.ErrNotExist
	}
	tmp := path + ".part"
	f, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o600)
	if err != nil {
		return err
	}
	_, err = io.Copy(f, io.LimitReader(resp.Body, maxPictureBytes))
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, path)
}

// block blocks or unblocks a contact, then publishes the new block list.
func (s *Session) block(cmd Command) {
	if s.client == nil || !s.client.IsLoggedIn() {
		return
	}
	jid, err := types.ParseJID(cmd.JID)
	if err != nil {
		return
	}
	action := events.BlocklistChangeActionUnblock
	if cmd.Block {
		action = events.BlocklistChangeActionBlock
	}
	ctx, cancel := context.WithTimeout(s.ctx, 30*time.Second)
	defer cancel()
	if list, err := s.client.UpdateBlocklist(ctx, jid, action); err == nil && list != nil {
		s.emitBlocklist(list.JIDs)
	}
}

// publishBlocklist sends the current block list (after connecting, and when it changes).
func (s *Session) publishBlocklist() {
	if s.client == nil {
		return
	}
	ctx, cancel := context.WithTimeout(s.ctx, 30*time.Second)
	defer cancel()
	if list, err := s.client.GetBlocklist(ctx); err == nil && list != nil {
		s.emitBlocklist(list.JIDs)
	}
}

func (s *Session) emitBlocklist(jids []types.JID) {
	out := make([]string, 0, len(jids))
	for _, j := range jids {
		out = append(out, s.phoneJID(j))
	}
	s.emit(map[string]any{"evt": "blocklist", "jids": out})
}

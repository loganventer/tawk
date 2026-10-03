# tawk: WhatsApp in your terminal
#
# Run `make help` for every target and option.

APP        ?= tawk
VERSION    ?= 0.8.0
# The commit this build comes from, so `tawk --update` can tell whether it is current.
COMMIT     ?= $(shell git rev-parse HEAD 2>/dev/null)

# Install layout follows the Filesystem Hierarchy Standard: self-built
# software goes under /usr/local (packagers use PREFIX=/usr, users without
# root use PREFIX=$$HOME/.local). Read-only data lives in share/, the
# internal Node.js program in lib/. Per-user files follow XDG at runtime.
PREFIX     ?= /usr/local
DESTDIR    ?=
BINDIR     := $(PREFIX)/bin
SHAREDIR   := $(PREFIX)/share/$(APP)
LIBDIR     := $(PREFIX)/lib/$(APP)
MANDIR     := $(PREFIX)/share/man/man1
DOCDIR     := $(PREFIX)/share/doc/$(APP)
COMPDIR    := $(PREFIX)/share/bash-completion/completions

BUILD      := build
UNAME_S    := $(shell uname -s)

# ---- output -----------------------------------------------------------------
# Short, readable progress lines by default; `make V=1` prints every command.

V ?= 0
ifeq ($(V),1)
  Q :=
else
  Q := @
endif

COLOR := $(shell test -t 1 && command -v tput >/dev/null 2>&1 && test "$$(tput colors 2>/dev/null)" -ge 8 2>/dev/null && echo 1)
ifeq ($(COLOR),1)
  C_HEAD := \033[1;32m
  C_STEP := \033[1;36m
  C_DIM  := \033[2m
  C_WARN := \033[1;33m
  C_OFF  := \033[0m
endif

say  = @printf '$(C_HEAD)==>$(C_OFF) %s\n' "$(1)"
step = @printf '  $(C_STEP)%-6s$(C_OFF) %s\n' "$(1)" "$(2)"
note = @printf '  $(C_DIM)%s$(C_OFF)\n' "$(1)"
warn = @printf '$(C_WARN)!!$(C_OFF)  %s\n' "$(1)"

# ---- toolchain and dependencies ---------------------------------------------

GO         ?= go
NPM        ?= npm
HAVE_GO    := $(shell command -v $(GO) >/dev/null 2>&1 && echo 1 || echo 0)
WHATSMEOW  ?= $(HAVE_GO)
GO_VERSION := $(shell $(GO) env GOVERSION 2>/dev/null)
# Compiling the SQLite package the bridge uses takes a lot of memory. With
# little of it free, Go builds one package at a time instead of several, so
# the compiler is not killed. GO_JOBS=N overrides it.
MEM_FREE_MB := $(shell awk '/^MemAvailable:/ {m=$$2} /^SwapFree:/ {s=$$2} END {if (m) print int((m+s)/1024)}' /proc/meminfo 2>/dev/null)
GO_JOBS    ?= $(if $(MEM_FREE_MB),$(shell [ $(MEM_FREE_MB) -lt 4096 ] && echo 1),)
HAVE_NODE  := $(shell command -v node >/dev/null 2>&1 && echo 1 || echo 0)

# ncurses with wide-character support
NCURSES_PC     := $(shell pkg-config --exists ncursesw && echo ncursesw || echo ncurses)
NCURSES_CFLAGS := $(shell pkg-config --cflags $(NCURSES_PC) 2>/dev/null)
NCURSES_LIBS   := $(shell pkg-config --libs $(NCURSES_PC) 2>/dev/null || echo -lncursesw)
NCURSES_VER    := $(shell pkg-config --modversion $(NCURSES_PC) 2>/dev/null || echo unknown)
SQLITE_CFLAGS  := $(shell pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS    := $(shell pkg-config --libs sqlite3 2>/dev/null || echo -lsqlite3)
SQLITE_VER     := $(shell pkg-config --modversion sqlite3 2>/dev/null || echo unknown)
# SQLCipher (SQLite with encryption) replaces SQLite when it is installed,
# so the database can be encrypted with a passphrase. SQLCIPHER=0 leaves it out.
SQLCIPHER      ?= $(shell pkg-config --exists sqlcipher 2>/dev/null && echo 1 || echo 0)
ifeq ($(SQLCIPHER),1)
  SQLITE_CFLAGS := $(shell pkg-config --cflags sqlcipher 2>/dev/null) -DAPP_WITH_SQLCIPHER -DSQLITE_HAS_CODEC
  SQLITE_LIBS   := $(shell pkg-config --libs sqlcipher 2>/dev/null || echo -lsqlcipher)
  SQLITE_VER    := SQLCipher $(shell pkg-config --modversion sqlcipher 2>/dev/null), database encryption available
else
  SQLITE_VER    := $(SQLITE_VER) (no SQLCipher: the database cannot be encrypted)
endif

CC       ?= cc
CC_VER   := $(shell $(CC) --version 2>/dev/null | head -n 1)
CSTD     := -std=c11
WARN     := -Wall -Wextra -Wpedantic -Wformat=2 -Wformat-security -Wshadow -Wno-unused-parameter
HARDEN   := -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE
DEFINES  := -D_GNU_SOURCE -D_DEFAULT_SOURCE -D_XOPEN_SOURCE_EXTENDED \
            -DAPP_NAME=\"$(APP)\" -DAPP_VERSION=\"$(VERSION)\" -DAPP_COMMIT=\"$(COMMIT)\" -DAPP_SHARE_DIR=\"$(SHAREDIR)\" -DAPP_LIB_DIR=\"$(LIBDIR)\"
CFLAGS   ?= -O2 -g
CFLAGS   += $(CSTD) $(WARN) $(HARDEN) $(DEFINES) -Iinclude -Ivendor/cjson -Ivendor/stb $(NCURSES_CFLAGS) $(SQLITE_CFLAGS)
LDFLAGS  += -pie
LDLIBS   := $(NCURSES_LIBS) $(SQLITE_LIBS) -lpthread -lm

ifeq ($(UNAME_S),Linux)
  LDFLAGS += -Wl,-z,relro,-z,now
  LDLIBS  += -lutil -ldl
  PLATFORM := Linux$(shell grep -qiE 'microsoft|wsl' /proc/sys/kernel/osrelease 2>/dev/null && echo ' (WSL)')
else ifeq ($(UNAME_S),Darwin)
  LDLIBS  += -framework CoreFoundation -framework Security -lresolv
  PLATFORM := macOS
else
  PLATFORM := $(UNAME_S)
endif

SRC      := $(shell find src -name '*.c') vendor/cjson/cJSON.c
OBJ      := $(patsubst %.c,$(BUILD)/%.o,$(SRC))
DEP      := $(OBJ:.o=.d)

WM_LIB   := $(BUILD)/bridge/libtawkwm.a
WM_SRC   := $(wildcard bridge/whatsmeow/*.go) bridge/whatsmeow/go.mod bridge/whatsmeow/go.sum
ifeq ($(WHATSMEOW),1)
  CFLAGS += -DAPP_WITH_WHATSMEOW -I$(BUILD)/bridge
  LINK_WM := $(WM_LIB)
  BACKEND_TEXT := whatsmeow in-process ($(GO_VERSION)), Node.js sidecar optional
else ifeq ($(HAVE_GO),0)
  BACKEND_TEXT := Node.js sidecar only (Go not found; install Go 1.21+ for the in-process backend)
else
  BACKEND_TEXT := Node.js sidecar only (WHATSMEOW=0)
endif
ifeq ($(wildcard sidecar/node_modules),)
  SIDECAR_TEXT := not prepared (run 'make sidecar' to include the Node.js backend)
else
  SIDECAR_TEXT := ready, will be installed
endif

.PHONY: logos screenshots all config bridge sidecar install uninstall clean distclean check test help

# ---- build ------------------------------------------------------------------

all: config $(APP)
	@printf '$(C_HEAD)==>$(C_OFF) %s\n' "$(APP) is ready: $$(du -h $(APP) | cut -f1) at ./$(APP)"
	$(call note,run it now:      ./$(APP))
	$(call note,install system:  sudo make install)
	$(call note,install for you: make install PREFIX=$$HOME/.local)

config:
	$(call say,Configuring $(APP) $(VERSION))
	@printf '  %-11s %s\n' "platform"  "$(PLATFORM)"
	@printf '  %-11s %s\n' "compiler"  "$(CC_VER)"
	@printf '  %-11s %s\n' "ncurses"   "$(NCURSES_PC) $(NCURSES_VER)"
	@printf '  %-11s %s\n' "sqlite"    "$(SQLITE_VER)"
	@printf '  %-11s %s\n' "backend"   "$(BACKEND_TEXT)"
	@printf '  %-11s %s\n' "sidecar"   "$(SIDECAR_TEXT)"
	@printf '  %-11s %s\n' "prefix"    "$(PREFIX)"
	@printf '  %-11s %s\n' "output"    "$(if $(filter 1,$(V)),full commands (V=1),short (make V=1 shows full commands))"
	@command -v ffmpeg >/dev/null 2>&1 || printf '$(C_WARN)!!$(C_OFF)  %s\n' "ffmpeg not found: voice notes will not record or play until it is installed"

$(APP): $(LINK_WM) $(OBJ)
	$(call say,Linking)
	$(call step,LINK,$(APP) ($(words $(OBJ)) objects$(if $(LINK_WM), + whatsmeow bridge)))
	$(Q)$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LINK_WM) $(LDLIBS)

# Print the configuration before anything else, even under make -j.
$(OBJ) $(WM_LIB): | config

# The whatsmeow header must exist before the gateway is compiled.
$(BUILD)/src/resource_access/whatsmeow_gateway.o: $(LINK_WM)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(call step,CC,$<)
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

bridge: $(WM_LIB)

$(WM_LIB): $(WM_SRC)
	$(call say,Building the whatsmeow bridge (Go $(GO_VERSION)))
	$(call note,the first build downloads Go modules and can take a minute or two)
	@mkdir -p $(dir $@)
	$(call step,GO,bridge/whatsmeow -> $@)
	$(if $(GO_JOBS),$(call note,building one package at a time: about $(MEM_FREE_MB) MB of memory is free))
	$(Q)cd bridge/whatsmeow && CGO_ENABLED=1 $(GO) build $(if $(GO_JOBS),-p $(GO_JOBS)) -trimpath -ldflags "-s -w" -buildmode=c-archive -o $(CURDIR)/$@ . || { \
	  printf '$(C_WARN)!!$(C_OFF) %s\n' "the Go build failed. If it says 'signal: killed', the computer ran out of memory:" \
	    "   add swap (for example: sudo fallocate -l 4G /swapfile && sudo chmod 600 /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile)" \
	    "   and run make again, or build without it: make WHATSMEOW=0 (then make sidecar for the Node.js backend)"; \
	  exit 1; }

sidecar: sidecar/node_modules
	$(call say,Node.js sidecar is ready ($$(node --version 2>/dev/null || echo node missing)))

sidecar/node_modules: sidecar/package.json sidecar/package-lock.json
	$(call say,Installing the Node.js sidecar dependencies (npm ci, pinned by package-lock.json))
	$(Q)cd sidecar && $(NPM) ci --omit=dev --no-audit --no-fund
	@touch $@

# ---- install ----------------------------------------------------------------

install: $(APP)
	$(call say,Installing $(APP) $(VERSION) to $(DESTDIR)$(PREFIX))
	$(Q)install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(SHAREDIR)/themes $(DESTDIR)$(SHAREDIR)/sounds $(DESTDIR)$(SHAREDIR)/emoji \
	           $(DESTDIR)$(MANDIR) $(DESTDIR)$(DOCDIR) $(DESTDIR)$(COMPDIR)
	$(Q)install -m 0755 $(APP) $(DESTDIR)$(BINDIR)/$(APP)
	$(call step,bin,$(BINDIR)/$(APP))
	@f="$(DESTDIR)$(BINDIR)/$(APP)"; \
	  size=$$(du -h "$$f" | cut -f1); \
	  mode=$$(stat -c '%A' "$$f" 2>/dev/null || stat -f '%Sp' "$$f"); \
	  sum=$$( (sha256sum "$$f" 2>/dev/null || shasum -a 256 "$$f") | cut -c1-16); \
	  printf '  %-6s %s\n' "" "$$size, $$mode, sha256 $$sum..."; \
	  printf '  %-6s %s\n' "" "backend: $(BACKEND_TEXT)"; \
	  if [ -z "$(DESTDIR)" ]; then \
	    v=$$("$$f" --version 2>&1 | head -n 1) && printf '  %-6s %s\n' "" "verified: $$v runs from $(BINDIR)" \
	      || printf '$(C_WARN)!!$(C_OFF)  %s\n' "the installed binary did not run: $$v"; \
	  fi
	$(Q)install -m 0644 themes/*.json $(DESTDIR)$(SHAREDIR)/themes/
	$(call step,themes,$(SHAREDIR)/themes ($(words $(wildcard themes/*.json)) themes))
	$(Q)install -m 0644 assets/sounds/*.wav $(DESTDIR)$(SHAREDIR)/sounds/
	$(call step,sounds,$(SHAREDIR)/sounds)
	$(Q)install -d $(DESTDIR)$(SHAREDIR)/emoji && install -m 0644 assets/emoji/emoji.tsv $(DESTDIR)$(SHAREDIR)/emoji/
	$(call step,emoji,$(SHAREDIR)/emoji ($$(grep -vc '^#' assets/emoji/emoji.tsv) emoji, Unicode data))
	$(Q)sed 's/@APP@/$(APP)/g; s/@VERSION@/$(VERSION)/g' docs/tawk.1 > $(DESTDIR)$(MANDIR)/$(APP).1
	$(Q)chmod 0644 $(DESTDIR)$(MANDIR)/$(APP).1
	$(call step,man,$(MANDIR)/$(APP).1  (man $(APP)))
	$(Q)install -m 0644 *.md LICENSE $(DESTDIR)$(DOCDIR)/
	$(Q)install -d -m 0755 $(DESTDIR)$(DOCDIR)/docs/images
	$(Q)install -m 0644 docs/images/*.png $(DESTDIR)$(DOCDIR)/docs/images/
	$(call step,docs,$(DOCDIR))
	$(Q)sed 's/@APP@/$(APP)/g' completions/tawk.bash > $(DESTDIR)$(COMPDIR)/$(APP)
	$(Q)chmod 0644 $(DESTDIR)$(COMPDIR)/$(APP)
	$(call step,bash,$(COMPDIR)/$(APP)  (tab completion))
	@if [ -d sidecar/node_modules ]; then \
	  install -d $(DESTDIR)$(LIBDIR)/sidecar; \
	  rm -rf $(DESTDIR)$(LIBDIR)/sidecar/src $(DESTDIR)$(LIBDIR)/sidecar/node_modules; \
	  cp -R sidecar/src sidecar/node_modules sidecar/package.json $(DESTDIR)$(LIBDIR)/sidecar/; \
	  printf '  $(C_STEP)%-6s$(C_OFF) %s\n' "node" "$(LIBDIR)/sidecar  (Baileys backend)"; \
	else \
	  printf '  $(C_DIM)%s$(C_OFF)\n' "skipped the Node.js sidecar; run 'make sidecar' first to include it"; \
	fi
	$(call say,Done)
	$(call note,your own files are created on first run:)
	$(call note,  ~/.config/$(APP)        settings and your themes)
	$(call note,  ~/.local/share/$(APP)   chats and the WhatsApp login)
	$(call note,  ~/.cache/$(APP)         downloaded media)
	$(call note,  ~/.local/state/$(APP)   logs)
	@case ":$$PATH:" in *":$(BINDIR):"*) printf '  %s\n' "run: $(APP)";; \
	  *) printf '$(C_WARN)!!$(C_OFF)  %s\n' "$(BINDIR) is not on your PATH; add it or run $(BINDIR)/$(APP)";; esac

uninstall:
	$(call say,Removing $(APP) from $(DESTDIR)$(PREFIX))
	$(Q)rm -f $(DESTDIR)$(BINDIR)/$(APP) $(DESTDIR)$(MANDIR)/$(APP).1 $(DESTDIR)$(COMPDIR)/$(APP)
	$(Q)rm -rf $(DESTDIR)$(SHAREDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(DOCDIR)
	$(call note,your own files were kept: ~/.config/$(APP) ~/.local/share/$(APP) ~/.cache/$(APP) ~/.local/state/$(APP))

# ---- housekeeping -------------------------------------------------------------

check: $(APP)
	$(call say,Smoke test)
	$(Q)./$(APP) --version

# Tests that need no WhatsApp account; run on Linux and macOS in CI.
TESTS := $(BUILD)/tests/pty_idle_action_test $(BUILD)/tests/ncurses_mouse_queue_test $(BUILD)/tests/chat_list_drag_test $(BUILD)/tests/attach_photo_test \
         $(BUILD)/tests/chat_list_fold_test $(BUILD)/tests/message_receipts_test $(BUILD)/tests/connection_recovery_test \
         $(BUILD)/tests/profile_status_test $(BUILD)/tests/account_dialogs_test $(BUILD)/tests/status_feed_test $(BUILD)/tests/migration_test \
         $(BUILD)/tests/markup_test $(BUILD)/tests/mentions_test $(BUILD)/tests/link_preview_test \
         $(BUILD)/tests/database_crypt_test $(BUILD)/tests/backup_test $(BUILD)/tests/forward_test $(BUILD)/tests/schedule_test \
         $(BUILD)/tests/status_answer_test $(BUILD)/tests/message_search_test \
         $(BUILD)/tests/control_transport_test $(BUILD)/tests/control_protocol_test $(BUILD)/tests/status_photo_viewer_test \
         $(BUILD)/tests/status_auto_advance_test $(BUILD)/tests/message_window_test \
         $(BUILD)/tests/chat_toggle_test $(BUILD)/tests/account_store_test

test: $(TESTS)
	$(call say,Running tests)
	$(Q)for t in $(TESTS); do $$t || exit 1; done

$(BUILD)/tests/pty_idle_action_test: $(BUILD)/tests/pty_idle_action_test.o $(BUILD)/src/infrastructure/pty_idle_action.o $(BUILD)/src/utilities/log.o
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/tests/ncurses_mouse_queue_test: $(BUILD)/tests/ncurses_mouse_queue_test.o
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Links everything but main, so it can test any part of the TUI.
$(BUILD)/tests/chat_list_drag_test: $(BUILD)/tests/chat_list_drag_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/attach_photo_test: $(BUILD)/tests/attach_photo_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/chat_list_fold_test: $(BUILD)/tests/chat_list_fold_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/message_receipts_test: $(BUILD)/tests/message_receipts_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/message_search_test: $(BUILD)/tests/message_search_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/status_photo_viewer_test: $(BUILD)/tests/status_photo_viewer_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/status_auto_advance_test: $(BUILD)/tests/status_auto_advance_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/link_preview_test: $(BUILD)/tests/link_preview_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/mentions_test: $(BUILD)/tests/mentions_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/markup_test: $(BUILD)/tests/markup_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/backup_test: $(BUILD)/tests/backup_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/database_crypt_test: $(BUILD)/tests/database_crypt_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/schedule_test: $(BUILD)/tests/schedule_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/message_window_test: $(BUILD)/tests/message_window_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/chat_toggle_test: $(BUILD)/tests/chat_toggle_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/forward_test: $(BUILD)/tests/forward_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/status_answer_test: $(BUILD)/tests/status_answer_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/control_transport_test: $(BUILD)/tests/control_transport_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/control_protocol_test: $(BUILD)/tests/control_protocol_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/account_store_test: $(BUILD)/tests/account_store_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/migration_test: $(BUILD)/tests/migration_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/account_dialogs_test: $(BUILD)/tests/account_dialogs_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/status_feed_test: $(BUILD)/tests/status_feed_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/connection_recovery_test: $(BUILD)/tests/connection_recovery_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

$(BUILD)/tests/profile_status_test: $(BUILD)/tests/profile_status_test.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

# Screenshots of the newer screens for MANUAL.md, drawn by the app's own
# code with made-up data. Needs Python with Pillow and network access once
# for the emoji images.
SHOTS := $(BUILD)/screenshots

$(BUILD)/tools/screenshots/scenes: $(BUILD)/tools/screenshots/scenes.o $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(LINK_WM)
	$(call step,LD,$@)
	$(Q)$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LINK_WM) $(LDLIBS)

# The logo files, drawn as PNGs with a transparent background. Needs Python with Pillow and the DejaVu fonts.
logos:
	$(call say,Drawing the logo PNGs into docs/images)
	$(Q)python3 tools/branding/make_logos.py docs/images

screenshots: $(BUILD)/tools/screenshots/scenes
	$(call say,Drawing screenshots into docs/images)
	$(Q)mkdir -p $(SHOTS)
	$(Q)python3 tools/screenshots/render.py pictures $(SHOTS)
	$(Q)$(BUILD)/tools/screenshots/scenes $(SHOTS) $(SHOTS)
	$(Q)python3 tools/screenshots/render.py cells $(SHOTS) docs/images

clean:
	$(call say,Removing build output ($(BUILD)/ and ./$(APP)))
	$(Q)rm -rf $(BUILD) $(APP)

distclean: clean
	$(call say,Removing the Node.js sidecar dependencies)
	$(Q)rm -rf sidecar/node_modules

help:
	@printf '%s\n' \
	  "$(APP) $(VERSION): WhatsApp in your terminal" \
	  "" \
	  "Targets" \
	  "  make                 configure summary, build ./$(APP)" \
	  "  make install         install to PREFIX (default /usr/local; may need sudo)" \
	  "  make uninstall       remove an installed $(APP) (your own files are kept)" \
	  "  make sidecar         install the Node.js (Baileys) backend dependencies" \
	  "  make bridge          build only the whatsmeow Go bridge" \
	  "  make check           build and run ./$(APP) --version" \
	  "  make test            build and run the tests" \
	  "  make screenshots     draw the manual's pictures of newer screens into docs/images" \
	  "  make logos           draw the logo PNGs into docs/images" \
	  "  make clean           remove build output" \
	  "  make distclean       also remove sidecar/node_modules" \
	  "  make help            this text" \
	  "" \
	  "Options" \
	  "  PREFIX=DIR           install location, e.g. PREFIX=\$$HOME/.local (current: $(PREFIX))" \
	  "  DESTDIR=DIR          stage the install under DIR (for packaging)" \
	  "  WHATSMEOW=0          leave out the in-process backend (no Go needed)" \
	  "  SQLCIPHER=0          build without database encryption even when SQLCipher is installed" \
	  "  APP=NAME             build under another name (binary and folders)" \
	  "  V=1                  show full compiler and linker commands" \
	  "  CC=clang CFLAGS=..   choose the compiler and flags"

-include $(DEP)
# Test objects too, so a changed header (Settings, say) rebuilds them.
-include $(wildcard $(BUILD)/tests/*.d)

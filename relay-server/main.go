package main

import (
	"bufio"
	"crypto/rand"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"math/big"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	defaultAddr            = "127.0.0.1:8080"
	defaultRoomTTL         = 30 * time.Minute
	defaultMaxMessageBytes = int64(64 * 1024)
	defaultMaxRooms        = 1000
	defaultMaxConnections  = 2000
	defaultRateLimit       = 60
	defaultIdleTimeout     = 2 * time.Minute
	defaultRoomCodeLength  = 8
	roomCodeAlphabet       = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"
	websocketGUID          = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
)

type clientMessage struct {
	Type    string          `json:"type"`
	Room    string          `json:"room,omitempty"`
	Payload json.RawMessage `json:"payload,omitempty"`
}

type serverMessage struct {
	Type       string          `json:"type"`
	Room       string          `json:"room,omitempty"`
	Peer       string          `json:"peer,omitempty"`
	From       string          `json:"from,omitempty"`
	Reason     string          `json:"reason,omitempty"`
	TTLSeconds int64           `json:"ttl_seconds,omitempty"`
	Payload    json.RawMessage `json:"payload,omitempty"`
}

type hub struct {
	mu               sync.Mutex
	rooms            map[string]*room
	rateLimits       map[string]*rateLimit
	roomTTL          time.Duration
	idleTimeout      time.Duration
	maxMessageBytes  int64
	maxRooms         int
	maxConnections   int
	connections      int
	rateLimit        int
	roomCodeLength   int
	allowedOrigins   map[string]struct{}
	cleanupFrequency time.Duration
}

type rateLimit struct {
	windowStart time.Time
	count       int
}

type room struct {
	code    string
	peers   map[string]*peer
	created time.Time
	updated time.Time
}

type peer struct {
	mu     sync.Mutex
	id     string
	room   *room
	hub    *hub
	conn   *wsConn
	send   chan []byte
	closed bool
	ip     string
}

func main() {
	addr := envString("SHARENOTEPAD_RELAY_ADDR", envString("ADDR", defaultAddr))
	roomTTL := envDuration("SHARENOTEPAD_ROOM_TTL", envDuration("ROOM_TTL", defaultRoomTTL))
	idleTimeout := envDuration("SHARENOTEPAD_IDLE_TIMEOUT", defaultIdleTimeout)
	maxMessageBytes := envInt64("SHARENOTEPAD_MAX_MESSAGE_BYTES", envInt64("MAX_MESSAGE_BYTES", defaultMaxMessageBytes))
	maxRooms := envInt("SHARENOTEPAD_MAX_ROOMS", defaultMaxRooms)
	maxConnections := envInt("SHARENOTEPAD_MAX_CONNECTIONS", defaultMaxConnections)
	rateLimit := envInt("SHARENOTEPAD_RATE_LIMIT_PER_MINUTE", defaultRateLimit)
	roomCodeLength := envInt("SHARENOTEPAD_ROOM_CODE_LENGTH", defaultRoomCodeLength)
	allowedOrigins := envSet("SHARENOTEPAD_ALLOWED_ORIGINS")

	h := &hub{
		rooms:            make(map[string]*room),
		rateLimits:       make(map[string]*rateLimit),
		roomTTL:          roomTTL,
		idleTimeout:      idleTimeout,
		maxMessageBytes:  maxMessageBytes,
		maxRooms:         maxRooms,
		maxConnections:   maxConnections,
		rateLimit:        rateLimit,
		roomCodeLength:   roomCodeLength,
		allowedOrigins:   allowedOrigins,
		cleanupFrequency: time.Minute,
	}
	go h.cleanupLoop()

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		_, _ = w.Write([]byte(`{"ok":true}`))
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		_, _ = w.Write([]byte("ShareNotepad relay server\n"))
	})
	mux.HandleFunc("/ws", h.handleWebSocket)

	server := &http.Server{
		Addr:              addr,
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	if len(allowedOrigins) == 0 {
		log.Printf("warning: SHARENOTEPAD_ALLOWED_ORIGINS is empty; accepting any WebSocket Origin")
	}
	log.Printf(
		"ShareNotepad relay listening on %s room_ttl=%s idle_timeout=%s max_message_bytes=%d max_rooms=%d max_connections=%d",
		addr,
		roomTTL,
		idleTimeout,
		maxMessageBytes,
		maxRooms,
		maxConnections,
	)
	if err := server.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
}

func envString(name string, fallback string) string {
	value := strings.TrimSpace(os.Getenv(name))
	if value == "" {
		return fallback
	}
	return value
}

func envDuration(name string, fallback time.Duration) time.Duration {
	value := strings.TrimSpace(os.Getenv(name))
	if value == "" {
		return fallback
	}
	duration, err := time.ParseDuration(value)
	if err != nil {
		log.Printf("invalid %s=%q, using %s", name, value, fallback)
		return fallback
	}
	return duration
}

func envInt64(name string, fallback int64) int64 {
	value := strings.TrimSpace(os.Getenv(name))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.ParseInt(value, 10, 64)
	if err != nil || parsed <= 0 {
		log.Printf("invalid %s=%q, using %d", name, value, fallback)
		return fallback
	}
	return parsed
}

func envInt(name string, fallback int) int {
	value := strings.TrimSpace(os.Getenv(name))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.Atoi(value)
	if err != nil || parsed <= 0 {
		log.Printf("invalid %s=%q, using %d", name, value, fallback)
		return fallback
	}
	return parsed
}

func envSet(name string) map[string]struct{} {
	value := strings.TrimSpace(os.Getenv(name))
	if value == "" {
		return nil
	}

	result := make(map[string]struct{})
	for _, item := range strings.Split(value, ",") {
		item = strings.TrimSpace(item)
		if item != "" {
			result[item] = struct{}{}
		}
	}
	return result
}

func (h *hub) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	ip := h.clientIP(r)
	if !h.allowRateLimitedAction(ip) {
		http.Error(w, "rate limited", http.StatusTooManyRequests)
		return
	}

	if !h.registerConnection() {
		http.Error(w, "server busy", http.StatusServiceUnavailable)
		return
	}
	conn, err := acceptWebSocket(w, r, h.maxMessageBytes, h.idleTimeout, h.allowedOrigins)
	if err != nil {
		h.unregisterConnection()
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	defer h.unregisterConnection()

	p := &peer{
		hub:  h,
		conn: conn,
		send: make(chan []byte, 32),
		ip:   ip,
	}

	go p.writeLoop()
	p.readLoop()
	h.removePeer(p)
	p.close()
}

func (p *peer) readLoop() {
	defer p.conn.Close()

	for {
		data, err := p.conn.readText()
		if err != nil {
			return
		}

		var message clientMessage
		if err := json.Unmarshal(data, &message); err != nil {
			p.sendJSON(serverMessage{Type: "ERROR", Reason: "invalid_json"})
			continue
		}

		message.Type = strings.ToUpper(strings.TrimSpace(message.Type))
		switch message.Type {
		case "CREATE":
			p.hub.createRoom(p)
		case "JOIN":
			p.hub.joinRoom(p, message.Room)
		case "RELAY":
			p.hub.relayPayload(p, message.Payload)
		case "PING":
			p.sendJSON(serverMessage{Type: "PONG"})
		case "LEAVE":
			return
		default:
			p.hub.relayRaw(p, data)
		}
	}
}

func (h *hub) clientIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}

	parsed := net.ParseIP(host)
	if parsed == nil {
		return host
	}

	if parsed.IsLoopback() {
		forwarded := strings.TrimSpace(r.Header.Get("X-Forwarded-For"))
		if forwarded != "" {
			first := strings.TrimSpace(strings.Split(forwarded, ",")[0])
			if net.ParseIP(first) != nil {
				return first
			}
		}
	}

	return parsed.String()
}

func (h *hub) registerConnection() bool {
	h.mu.Lock()
	defer h.mu.Unlock()

	if h.connections >= h.maxConnections {
		return false
	}
	h.connections++
	return true
}

func (h *hub) unregisterConnection() {
	h.mu.Lock()
	if h.connections > 0 {
		h.connections--
	}
	h.mu.Unlock()
}

func (h *hub) allowRateLimitedAction(ip string) bool {
	if h.rateLimit <= 0 {
		return true
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	now := time.Now()
	limit := h.rateLimits[ip]
	if limit == nil || now.Sub(limit.windowStart) >= time.Minute {
		h.rateLimits[ip] = &rateLimit{windowStart: now, count: 1}
		return true
	}

	if limit.count >= h.rateLimit {
		return false
	}

	limit.count++
	return true
}

func (p *peer) writeLoop() {
	for data := range p.send {
		if err := p.conn.writeText(data); err != nil {
			return
		}
	}
	_ = p.conn.writeClose()
}

func (p *peer) sendJSON(message serverMessage) {
	data, err := json.Marshal(message)
	if err != nil {
		return
	}
	p.sendRaw(data)
}

func (p *peer) sendRaw(data []byte) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.closed {
		return
	}

	select {
	case p.send <- data:
	default:
		go p.close()
	}
}

func (p *peer) close() {
	p.mu.Lock()
	if !p.closed {
		p.closed = true
		close(p.send)
	}
	p.mu.Unlock()
	_ = p.conn.Close()
}

func (h *hub) createRoom(p *peer) {
	if !h.allowRateLimitedAction(p.ip) {
		p.sendJSON(serverMessage{Type: "ERROR", Reason: "rate_limited"})
		return
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	if p.room != nil {
		p.sendJSON(serverMessage{Type: "ERROR", Reason: "already_joined"})
		return
	}
	if len(h.rooms) >= h.maxRooms {
		p.sendJSON(serverMessage{Type: "ERROR", Reason: "server_busy"})
		return
	}

	code, err := h.generateRoomCodeLocked()
	if err != nil {
		p.sendJSON(serverMessage{Type: "ERROR", Reason: "room_code_failed"})
		return
	}

	now := time.Now()
	r := &room{
		code:    code,
		peers:   make(map[string]*peer),
		created: now,
		updated: now,
	}
	p.id = "A"
	p.room = r
	r.peers[p.id] = p
	h.rooms[code] = r

	p.sendJSON(serverMessage{
		Type:       "ROOM_CREATED",
		Room:       code,
		Peer:       p.id,
		TTLSeconds: int64(h.roomTTL.Seconds()),
	})
	log.Printf("room created")
}

func (h *hub) joinRoom(p *peer, code string) {
	if !h.allowRateLimitedAction(p.ip) {
		p.sendJSON(serverMessage{Type: "JOIN_FAIL", Room: code, Reason: "rate_limited"})
		return
	}

	code = strings.ToUpper(strings.TrimSpace(code))
	if !isValidRoomCode(code) {
		p.sendJSON(serverMessage{Type: "JOIN_FAIL", Room: code, Reason: "invalid_code"})
		return
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	if p.room != nil {
		p.sendJSON(serverMessage{Type: "JOIN_FAIL", Room: code, Reason: "already_joined"})
		return
	}

	r := h.rooms[code]
	if r == nil {
		p.sendJSON(serverMessage{Type: "JOIN_FAIL", Room: code, Reason: "invalid_code"})
		return
	}
	if time.Since(r.updated) > h.roomTTL {
		delete(h.rooms, code)
		p.sendJSON(serverMessage{Type: "JOIN_FAIL", Room: code, Reason: "expired_code"})
		return
	}
	if len(r.peers) >= 2 {
		p.sendJSON(serverMessage{Type: "JOIN_FAIL", Room: code, Reason: "room_full"})
		return
	}

	p.id = nextPeerID(r)
	p.room = r
	r.peers[p.id] = p
	r.updated = time.Now()

	p.sendJSON(serverMessage{
		Type:       "JOIN_OK",
		Room:       code,
		Peer:       p.id,
		TTLSeconds: int64(h.roomTTL.Seconds()),
	})
	h.notifyRoomLocked(r, p, serverMessage{Type: "PEER_JOINED", Room: code, Peer: p.id})
	log.Printf("peer joined peer=%s", p.id)
}

func (h *hub) relayPayload(p *peer, payload json.RawMessage) {
	if len(payload) == 0 {
		p.sendJSON(serverMessage{Type: "ERROR", Reason: "empty_payload"})
		return
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	if p.room == nil {
		p.sendJSON(serverMessage{Type: "ERROR", Reason: "not_joined"})
		return
	}

	p.room.updated = time.Now()
	h.notifyRoomLocked(p.room, p, serverMessage{
		Type:    "RELAY",
		Room:    p.room.code,
		From:    p.id,
		Payload: payload,
	})
}

func (h *hub) relayRaw(p *peer, raw json.RawMessage) {
	h.mu.Lock()
	defer h.mu.Unlock()

	if p.room == nil {
		p.sendJSON(serverMessage{Type: "ERROR", Reason: "not_joined"})
		return
	}

	p.room.updated = time.Now()
	h.notifyRoomLocked(p.room, p, serverMessage{
		Type:    "RELAY",
		Room:    p.room.code,
		From:    p.id,
		Payload: raw,
	})
}

func (h *hub) removePeer(p *peer) {
	h.mu.Lock()
	defer h.mu.Unlock()

	if p.room == nil {
		return
	}

	r := p.room
	delete(r.peers, p.id)
	p.room = nil

	if len(r.peers) == 0 {
		delete(h.rooms, r.code)
		log.Printf("room removed code=%s", r.code)
		return
	}

	r.updated = time.Now()
	h.notifyRoomLocked(r, p, serverMessage{Type: "PEER_LEFT", Room: r.code, Peer: p.id})
}

func (h *hub) notifyRoomLocked(r *room, except *peer, message serverMessage) {
	if r == nil {
		return
	}
	for _, other := range r.peers {
		if other != except {
			other.sendJSON(message)
		}
	}
}

func (h *hub) generateRoomCodeLocked() (string, error) {
	for attempt := 0; attempt < 100; attempt++ {
		code, err := randomRoomCode(h.roomCodeLength)
		if err != nil {
			return "", err
		}
		if h.rooms[code] == nil {
			return code, nil
		}
	}
	return "", errors.New("room code collision")
}

func (h *hub) cleanupLoop() {
	ticker := time.NewTicker(h.cleanupFrequency)
	defer ticker.Stop()

	for range ticker.C {
		h.cleanupExpiredRooms()
	}
}

func (h *hub) cleanupExpiredRooms() {
	h.mu.Lock()
	defer h.mu.Unlock()

	now := time.Now()
	for ip, limit := range h.rateLimits {
		if now.Sub(limit.windowStart) >= 2*time.Minute {
			delete(h.rateLimits, ip)
		}
	}

	for code, r := range h.rooms {
		if now.Sub(r.updated) <= h.roomTTL {
			continue
		}

		for _, p := range r.peers {
			p.sendJSON(serverMessage{Type: "ROOM_EXPIRED", Room: code})
			go p.close()
		}
		delete(h.rooms, code)
		log.Printf("room expired")
	}
}

func nextPeerID(r *room) string {
	if r.peers["A"] == nil {
		return "A"
	}
	return "B"
}

func randomRoomCode(length int) (string, error) {
	if length < 6 {
		length = 6
	}
	if length > 32 {
		length = 32
	}

	code := make([]byte, length)
	alphabetSize := big.NewInt(int64(len(roomCodeAlphabet)))
	for index := range code {
		value, err := rand.Int(rand.Reader, alphabetSize)
		if err != nil {
			return "", err
		}
		code[index] = roomCodeAlphabet[value.Int64()]
	}
	return string(code), nil
}

func isValidRoomCode(code string) bool {
	if len(code) < 6 || len(code) > 32 {
		return false
	}
	for _, ch := range code {
		if !strings.ContainsRune(roomCodeAlphabet, ch) {
			return false
		}
	}
	return true
}

type wsConn struct {
	conn            net.Conn
	reader          *bufio.Reader
	writeMu         sync.Mutex
	maxMessageBytes int64
	idleTimeout     time.Duration
}

func acceptWebSocket(
	w http.ResponseWriter,
	r *http.Request,
	maxMessageBytes int64,
	idleTimeout time.Duration,
	allowedOrigins map[string]struct{},
) (*wsConn, error) {
	if r.Method != http.MethodGet {
		return nil, errors.New("websocket requires GET")
	}
	if !headerContains(r.Header, "Connection", "Upgrade") ||
		!strings.EqualFold(r.Header.Get("Upgrade"), "websocket") {
		return nil, errors.New("missing websocket upgrade headers")
	}
	if r.Header.Get("Sec-WebSocket-Version") != "13" {
		return nil, errors.New("unsupported websocket version")
	}
	if !originAllowed(r.Header.Get("Origin"), allowedOrigins) {
		return nil, errors.New("websocket origin is not allowed")
	}

	key := strings.TrimSpace(r.Header.Get("Sec-WebSocket-Key"))
	decodedKey, err := base64.StdEncoding.DecodeString(key)
	if key == "" || err != nil || len(decodedKey) != 16 {
		return nil, errors.New("missing websocket key")
	}

	hijacker, ok := w.(http.Hijacker)
	if !ok {
		return nil, errors.New("http hijacking is not supported")
	}

	netConn, rw, err := hijacker.Hijack()
	if err != nil {
		return nil, err
	}

	accept := websocketAcceptKey(key)
	response := "HTTP/1.1 101 Switching Protocols\r\n" +
		"Upgrade: websocket\r\n" +
		"Connection: Upgrade\r\n" +
		"Sec-WebSocket-Accept: " + accept + "\r\n\r\n"
	if _, err := rw.WriteString(response); err != nil {
		_ = netConn.Close()
		return nil, err
	}
	if err := rw.Flush(); err != nil {
		_ = netConn.Close()
		return nil, err
	}

	return &wsConn{
		conn:            netConn,
		reader:          rw.Reader,
		maxMessageBytes: maxMessageBytes,
		idleTimeout:     idleTimeout,
	}, nil
}

func originAllowed(origin string, allowedOrigins map[string]struct{}) bool {
	if len(allowedOrigins) == 0 {
		return true
	}
	origin = strings.TrimSpace(origin)
	if origin == "" {
		return false
	}
	_, ok := allowedOrigins[origin]
	return ok
}

func websocketAcceptKey(key string) string {
	hash := sha1.Sum([]byte(key + websocketGUID))
	return base64.StdEncoding.EncodeToString(hash[:])
}

func headerContains(header http.Header, name string, expected string) bool {
	for _, value := range header.Values(name) {
		for _, part := range strings.Split(value, ",") {
			if strings.EqualFold(strings.TrimSpace(part), expected) {
				return true
			}
		}
	}
	return false
}

func (c *wsConn) readText() ([]byte, error) {
	for {
		opcode, payload, err := c.readFrame()
		if err != nil {
			return nil, err
		}

		switch opcode {
		case 0x1:
			return payload, nil
		case 0x8:
			return nil, io.EOF
		case 0x9:
			_ = c.writeFrame(0xA, payload)
		case 0xA:
			continue
		default:
			return nil, fmt.Errorf("unsupported websocket opcode: %d", opcode)
		}
	}
}

func (c *wsConn) readFrame() (byte, []byte, error) {
	if c.idleTimeout > 0 {
		if err := c.conn.SetReadDeadline(time.Now().Add(c.idleTimeout)); err != nil {
			return 0, nil, err
		}
	}

	header := make([]byte, 2)
	if _, err := io.ReadFull(c.reader, header); err != nil {
		return 0, nil, err
	}

	fin := header[0]&0x80 != 0
	opcode := header[0] & 0x0F
	if !fin {
		return 0, nil, errors.New("fragmented websocket frames are not supported")
	}

	masked := header[1]&0x80 != 0
	if !masked {
		return 0, nil, errors.New("client websocket frames must be masked")
	}

	length := int64(header[1] & 0x7F)
	switch length {
	case 126:
		extended := make([]byte, 2)
		if _, err := io.ReadFull(c.reader, extended); err != nil {
			return 0, nil, err
		}
		length = int64(binary.BigEndian.Uint16(extended))
	case 127:
		extended := make([]byte, 8)
		if _, err := io.ReadFull(c.reader, extended); err != nil {
			return 0, nil, err
		}
		extendedLength := binary.BigEndian.Uint64(extended)
		if extendedLength > uint64(c.maxMessageBytes) {
			return 0, nil, errors.New("websocket message too large")
		}
		length = int64(extendedLength)
	}

	if length < 0 || length > c.maxMessageBytes {
		return 0, nil, errors.New("websocket message too large")
	}

	maskKey := make([]byte, 4)
	if _, err := io.ReadFull(c.reader, maskKey); err != nil {
		return 0, nil, err
	}

	payload := make([]byte, length)
	if _, err := io.ReadFull(c.reader, payload); err != nil {
		return 0, nil, err
	}
	for index := range payload {
		payload[index] ^= maskKey[index%4]
	}

	return opcode, payload, nil
}

func (c *wsConn) writeText(payload []byte) error {
	return c.writeFrame(0x1, payload)
}

func (c *wsConn) writeClose() error {
	return c.writeFrame(0x8, nil)
}

func (c *wsConn) writeFrame(opcode byte, payload []byte) error {
	c.writeMu.Lock()
	defer c.writeMu.Unlock()

	if err := c.conn.SetWriteDeadline(time.Now().Add(5 * time.Second)); err != nil {
		return err
	}

	header := []byte{0x80 | opcode}
	length := len(payload)
	switch {
	case length <= 125:
		header = append(header, byte(length))
	case length <= 65535:
		header = append(header, 126, byte(length>>8), byte(length))
	default:
		header = append(header, 127)
		extended := make([]byte, 8)
		binary.BigEndian.PutUint64(extended, uint64(length))
		header = append(header, extended...)
	}

	if _, err := c.conn.Write(header); err != nil {
		return err
	}
	if len(payload) == 0 {
		return nil
	}
	_, err := c.conn.Write(payload)
	return err
}

func (c *wsConn) Close() error {
	return c.conn.Close()
}

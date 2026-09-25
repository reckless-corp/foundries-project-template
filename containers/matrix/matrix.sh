#!/bin/sh
# Streams "Matrix" digital rain to an HTTP client. Run under tcpsvd, which
# hands us the socket as stdin/stdout:
#   tcpsvd 0.0.0.0 8080 /bin/sh /matrix.sh
# Clients: curl "host:8080/?w=$(tput cols)"   (add &kana=1 for katakana)
# Browsers (Accept: text/html) get matrix.html, a canvas version, instead.
# Either way, half of requests get the Foundries "thumbs up guy" instead.

cr=$(printf '\r')
read -t 5 -r method path proto || exit 0
browser=0
while IFS= read -t 5 -r line; do
	[ -z "$line" ] || [ "$line" = "$cr" ] && break
	# browsers ask for text/html; curl sends "Accept: */*"
	case "$line" in [Aa]ccept:*text/html*) browser=1 ;; esac
done

case "$path" in
/ | /\?*) ;;
*)
	# favicon.ico and friends: don't hold the socket open with a stream
	printf 'HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'
	exit 0
	;;
esac

# coin flip: half the time, skip the rain and show the Foundries thumbs up guy
if [ $(($(od -An -N1 -tu1 /dev/urandom) % 2)) = 0 ]; then
	if [ "$browser" = 1 ]; then
		printf 'HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n'
		printf '<!doctype html><title>Thumbs Up</title><body style="background:#000;color:#0f0"><pre>\n'
	else
		printf 'HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n'
	fi
	cat <<'EOF'
            _  _
           | \/ |
        \__|____|__/
          |  o  o|           Thumbs Up
          |___\/_|_____||_
          |       _____|__|
          |      |
          |______|
          | |  | |
          | |  | |
          |_|  |_|
EOF
	[ "$browser" = 1 ] && printf '</pre>\n'
	exit 0
fi

if [ "$browser" = 1 ]; then
	printf 'HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n'
	exec cat "${0%/*}/matrix.html"
fi

W=40
case "$path" in
*w=[0-9]*)
	W=${path#*w=}
	W=${W%%[!0-9]*}
	[ "$W" -lt 20 ] && W=20
	[ "$W" -gt 400 ] && W=400
	;;
esac
# katakana looks more authentic but many terminal fonts lack the glyphs
KANA=0
case "$path" in *kana=1*) KANA=1 ;; esac

printf 'HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n'
printf '\033[?25l\033[2J'

exec awk -v w="$W" -v kana="$KANA" '
BEGIN {
	if (kana)
		n = split("ｱ ｲ ｳ ｴ ｵ ｶ ｷ ｸ ｹ ｺ ｻ ｼ ｽ ｾ ｿ ﾀ ﾁ ﾂ ﾃ ﾄ ﾅ ﾆ ﾇ ﾈ ﾉ ﾊ ﾋ ﾌ ﾍ ﾎ ﾏ ﾐ ﾑ ﾒ ﾓ ﾔ ﾕ ﾖ ﾗ ﾘ ﾙ ﾚ ﾛ ﾜ ﾝ 0 1 2 3 4 5 6 7 8 9 Z : . = * + - < >", ch, " ")
	else {
		s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789$+-*/=%#&@<>:;|^~"
		n = length(s)
		for (i = 1; i <= n; i++) ch[i] = substr(s, i, 1)
	}
	srand()
	prev = ""
	for (;;) {
		cur = ""; green = ""
		for (c = 1; c <= w; c++) {
			if (len[c] > 0) {
				k = ch[int(rand() * n) + 1]
				cur = cur k; green = green k
				len[c]--
			} else if (rand() < 0.02) {
				k = ch[int(rand() * n) + 1]
				# the leading edge of a streak is drawn bright white
				cur = cur "\033[1;97m" k "\033[0;32m"; green = green k
				len[c] = 4 + int(rand() * 20)
			} else {
				cur = cur " "; green = green " "
			}
		}
		# scroll the screen down one line, draw the new top row, and
		# repaint the previous row in plain green so heads fade
		printf "\033[H\033M\033[0;32m%s\033[2;1H%s", cur, prev
		fflush()
		prev = green
		system("sleep 0.06")
	}
}'

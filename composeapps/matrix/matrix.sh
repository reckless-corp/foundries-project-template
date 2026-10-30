#!/bin/sh
# Streams "Matrix" digital rain to an HTTP client. Run under tcpsvd, which
# hands us the socket as stdin/stdout:
#   tcpsvd 0.0.0.0 8080 /bin/sh /matrix.sh
# Clients: curl "host:8080/?w=$(tput cols)&h=$(tput lines)"
# Add &kana=1 for katakana. Pass terminal dimensions to center the figure.
# Browsers (Accept: text/html) get matrix.html, a canvas version, instead.
# The Foundries "thumbs up guy" stays centered while rain falls through his outline.

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
H=24
case "$path" in
*h=[0-9]*)
	H=${path#*h=}
	H=${H%%[!0-9]*}
	[ "$H" -lt 12 ] && H=12
	[ "$H" -gt 120 ] && H=120
	;;
esac
# katakana looks more authentic but many terminal fonts lack the glyphs
KANA=0
case "$path" in *kana=1*) KANA=1 ;; esac

printf 'HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n'
printf '\033[?25l\033[2J'

exec awk -v w="$W" -v h="$H" -v kana="$KANA" -v artfile="${0%/*}/thumbs.txt" '
BEGIN {
	if (kana)
		n = split("ｱ ｲ ｳ ｴ ｵ ｶ ｷ ｸ ｹ ｺ ｻ ｼ ｽ ｾ ｿ ﾀ ﾁ ﾂ ﾃ ﾄ ﾅ ﾆ ﾇ ﾈ ﾉ ﾊ ﾋ ﾌ ﾍ ﾎ ﾏ ﾐ ﾑ ﾒ ﾓ ﾔ ﾕ ﾖ ﾗ ﾘ ﾙ ﾚ ﾛ ﾜ ﾝ 0 1 2 3 4 5 6 7 8 9 Z : . = * + - < >", ch, " ")
	else {
		s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789$+-*/=%#&@<>:;|^~"
		n = length(s)
		for (i = 1; i <= n; i++) ch[i] = substr(s, i, 1)
	}
	# Store the underlying rain separately: scrolling painted artwork would
	# leave copies of the guy trailing down the screen.
	while ((getline line < artfile) > 0) {
		art[++ah] = line
		if (length(line) > aw) aw = length(line)
	}
	close(artfile)
	ax = int((w - aw) / 2)
	ay = int((h - ah) / 2)
	srand()
	for (;;) {
		for (r = h; r > 1; r--)
			for (c = 1; c <= w; c++) rain[r, c] = rain[r - 1, c]
		for (c = 1; c <= w; c++) head[c] = 0
		for (c = 1; c <= w; c++) {
			if (len[c] > 0) {
				k = ch[int(rand() * n) + 1]
				rain[1, c] = k
				len[c]--
			} else if (rand() < 0.02) {
				k = ch[int(rand() * n) + 1]
				# the leading edge of a streak is drawn bright white
				rain[1, c] = k; head[c] = 1
				len[c] = 4 + int(rand() * 20)
			} else {
				rain[1, c] = " "
			}
		}
		frame = ""
		for (r = 1; r <= h; r++) {
			frame = frame sprintf("\033[%d;1H", r)
			color = ""
			for (c = 1; c <= w; c++) {
				glyph = ""
				if (r > ay && r <= ay + ah && c > ax && c <= ax + aw)
					glyph = substr(art[r - ay], c - ax, 1)
				if (glyph != "" && glyph != " ") shade = "\033[1;97m"
				else {
					glyph = rain[r, c]
					if (glyph == "") glyph = " "
					shade = r == 1 && head[c] ? "\033[1;97m" : "\033[0;32m"
				}
				if (shade != color) { frame = frame shade; color = shade }
				frame = frame glyph
			}
		}
		printf "%s\033[0m", frame
		fflush()
		system("sleep 0.06")
	}
}'

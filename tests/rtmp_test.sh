#!/bin/sh
# Test de l'envoi RTMP contre ffmpeg en serveur (Linux ; ffmpeg 6.1 ou plus avec libx264, libx265 et libsvtav1).
# Usage : tests/rtmp_test.sh <dossier de travail> [h264|hevc|av1]
set -e
W=${1:-/tmp/rtmp_test}
CODEC=${2:-h264}
mkdir -p "$W"
cd "$(dirname "$0")/.."
g++ -std=c++17 -O1 -g -Wall -Wextra -pthread -fsanitize=address,undefined tests/rtmp_test.cpp layer/src/rtmp.cpp -o "$W/rtmp_test"
SRC="-f lavfi -i testsrc2=size=640x360:rate=60 -t 6"
case "$CODEC" in
  hevc) ffmpeg -loglevel error -y $SRC -c:v libx265 -preset veryfast -x265-params bframes=0:keyint=120:aud=1:log-level=error -f hevc "$W/video.bin" ;;
  av1) ffmpeg -loglevel error -y $SRC -c:v libsvtav1 -preset 10 -g 120 -svtav1-params pred-struct=1 -f obu "$W/video.bin" ;;
  *) ffmpeg -loglevel error -y $SRC -c:v libx264 -preset veryfast -bf 0 -g 120 -x264-params aud=1 -bsf:v h264_mp4toannexb -f h264 "$W/video.bin" ;;
esac
ffmpeg -loglevel error -y -f lavfi -i sine=frequency=440:sample_rate=48000 -ac 2 -t 6 -c:a aac -b:a 160k -f adts "$W/audio.aac"
rm -f "$W/recu.mkv"
ffmpeg -loglevel warning -y -listen 1 -timeout 15 -i rtmp://127.0.0.1:19350/app/clef -c copy "$W/recu.mkv" > "$W/serveur.txt" 2>&1 &
SERVER=$!
sleep 1.5
"$W/rtmp_test" "$W/video.bin" "$W/audio.aac" rtmp://127.0.0.1:19350/app clef 60 "$CODEC"
wait $SERVER || true
cat "$W/serveur.txt"
ffprobe -v error -show_entries stream=codec_name,profile,width,height,sample_rate,channels,nb_read_frames -count_frames -of compact "$W/recu.mkv"
ffprobe -v error -show_entries format=duration -of compact "$W/recu.mkv"

#!/bin/sh
# Test de l'envoi RTMP contre ffmpeg en serveur (Linux ; ffmpeg avec libx264). Usage : tests/rtmp_test.sh <dossier de travail>
set -e
W=${1:-/tmp/rtmp_test}
mkdir -p "$W"
cd "$(dirname "$0")/.."
g++ -std=c++17 -O1 -g -Wall -Wextra -pthread -fsanitize=address,undefined tests/rtmp_test.cpp layer/src/rtmp.cpp -o "$W/rtmp_test"
ffmpeg -loglevel error -y -f lavfi -i testsrc2=size=640x360:rate=60 -t 6 -c:v libx264 -preset veryfast -bf 0 -g 120 -x264-params aud=1 -bsf:v h264_mp4toannexb -f h264 "$W/video.h264"
ffmpeg -loglevel error -y -f lavfi -i sine=frequency=440:sample_rate=48000 -ac 2 -t 6 -c:a aac -b:a 160k -f adts "$W/audio.aac"
rm -f "$W/recu.flv"
ffmpeg -loglevel warning -y -listen 1 -timeout 15 -i rtmp://127.0.0.1:19350/app/clef -c copy "$W/recu.flv" > "$W/serveur.txt" 2>&1 &
SERVER=$!
sleep 1.5
"$W/rtmp_test" "$W/video.h264" "$W/audio.aac" rtmp://127.0.0.1:19350/app clef 60
wait $SERVER || true
ffprobe -v error -show_entries stream=codec_name,width,height,sample_rate,channels,nb_read_frames -count_frames -of compact "$W/recu.flv"
ffprobe -v error -show_entries format=duration -of compact "$W/recu.flv"

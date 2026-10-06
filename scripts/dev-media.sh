#!/bin/sh
# Generates a small fake media library for development: short test-pattern
# clips named after real open or public-domain films and shows, so Jellyfin
# finds their metadata and artwork. Total size is a few hundred MB.
#
# Usage: scripts/dev-media.sh [DIR]   (default: ~/.cache/ember-dev/media)
set -eu

dir=${1:-${EMBER_DEV_DIR:-$HOME/.cache/ember-dev}/media}
mkdir -p "$dir/movies" "$dir/shows"

# clip OUTPUT SECONDS SIZE TITLE [extra ffmpeg output options...]
clip() {
  out=$1 secs=$2 size=$3 title=$4
  shift 4
  [ -e "$out" ] && return 0
  mkdir -p "$(dirname "$out")"
  echo "making $out"
  ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=$size:rate=24,drawtext=text='$title':fontcolor=white:fontsize=h/12:x=(w-tw)/2:y=h*0.42:box=1:boxcolor=black@0.5" \
    -f lavfi -i "sine=frequency=330:sample_rate=48000,volume=0.05" \
    -t "$secs" "$@" "$out.part.${out##*.}"
  mv "$out.part.${out##*.}" "$out"
}

h264="-c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p -b:v 900k -c:a aac -b:a 96k"

clip "$dir/movies/Big Buck Bunny (2008)/Big Buck Bunny (2008).mkv" 180 1920x1080 "Big Buck Bunny" $h264
clip "$dir/movies/Sintel (2010)/Sintel (2010).mp4" 180 1920x1080 "Sintel" $h264
clip "$dir/movies/Elephants Dream (2006)/Elephants Dream (2006).mp4" 150 1280x720 "Elephants Dream" $h264
clip "$dir/movies/Spring (2019)/Spring (2019).mkv" 120 1920x1080 "Spring" $h264
clip "$dir/movies/Agent 327 Operation Barbershop (2017)/Agent 327 Operation Barbershop (2017).mkv" 120 1920x1080 "Agent 327" $h264
clip "$dir/movies/Charade (1963)/Charade (1963).mkv" 240 1280x720 "Charade" $h264
clip "$dir/movies/His Girl Friday (1940)/His Girl Friday (1940).mkv" 240 1280x720 "His Girl Friday" $h264
# HEVC 10-bit: the BRIX and the NUC can't decode it, so the server transcodes.
clip "$dir/movies/Tears of Steel (2012)/Tears of Steel (2012).mkv" 150 1920x1080 "Tears of Steel (HEVC 10-bit)" \
  -c:v libx265 -preset ultrafast -pix_fmt yuv420p10le -b:v 900k -x265-params log-level=error -c:a aac -b:a 96k
# VP9 in WebM.
clip "$dir/movies/Cosmos Laundromat (2015)/Cosmos Laundromat (2015).webm" 120 1280x720 "Cosmos Laundromat (VP9)" \
  -c:v libvpx-vp9 -deadline realtime -cpu-used 8 -b:v 900k -c:a libopus -b:a 96k

# Two audio languages (5.1 AC-3 English, stereo French), two subtitle
# languages, and chapters, in one file.
notld="$dir/movies/Night of the Living Dead (1968)/Night of the Living Dead (1968).mkv"
if [ ! -e "$notld" ]; then
  mkdir -p "$(dirname "$notld")"
  tmp=$(mktemp -d)
  printf '1\n00:00:02,000 --> 00:00:30,000\nThey are coming to get you, Barbara.\n\n2\n00:01:00,000 --> 00:01:30,000\nSecond subtitle line.\n' > "$tmp/en.srt"
  printf '1\n00:00:02,000 --> 00:00:30,000\nIls viennent te chercher, Barbara.\n' > "$tmp/fr.srt"
  printf ';FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=30000\ntitle=Intro\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=30000\nEND=200000\ntitle=Part One\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=200000\nEND=240000\ntitle=Credits\n' > "$tmp/chapters.txt"
  echo "making $notld"
  ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=1280x720:rate=24,drawtext=text='Night of the Living Dead':fontcolor=white:fontsize=h/12:x=(w-tw)/2:y=h*0.42:box=1:boxcolor=black@0.5" \
    -f lavfi -i "sine=frequency=220:sample_rate=48000,volume=0.05,pan=5.1|FL=c0|FR=c0|FC=c0|LFE=c0|BL=c0|BR=c0" \
    -f lavfi -i "sine=frequency=440:sample_rate=48000,volume=0.05" \
    -i "$tmp/en.srt" -i "$tmp/fr.srt" -i "$tmp/chapters.txt" \
    -map 0:v -map 1:a -map 2:a -map 3 -map 4 -map_metadata 5 -map_chapters 5 -t 240 \
    -c:v libx264 -preset ultrafast -pix_fmt yuv420p -b:v 900k \
    -c:a:0 ac3 -b:a:0 384k -c:a:1 aac -b:a:1 96k -c:s srt \
    -metadata:s:a:0 language=eng -metadata:s:a:0 title="English 5.1" \
    -metadata:s:a:1 language=fre -metadata:s:a:1 title="Français" \
    -metadata:s:s:0 language=eng -metadata:s:s:1 language=fre \
    "$notld.part.mkv"
  mv "$notld.part.mkv" "$notld"
  rm -r "$tmp"
fi

episode() {
  show=$1 year=$2 season=$3 number=$4
  s=$(printf '%02d' "$season") e=$(printf '%02d' "$number")
  clip "$dir/shows/$show ($year)/Season $s/$show ($year) - S${s}E${e}.mkv" 120 1280x720 "$show S${s}E${e}" $h264
}
for n in 1 2 3 4 5; do episode "The Twilight Zone" 1959 1 "$n"; done
for n in 1 2 3; do episode "The Twilight Zone" 1959 2 "$n"; done
for n in 1 2 3; do episode "Sherlock Holmes" 1954 1 "$n"; done
for n in 1 2; do episode "The Beverly Hillbillies" 1962 1 "$n"; done

du -sh "$dir"

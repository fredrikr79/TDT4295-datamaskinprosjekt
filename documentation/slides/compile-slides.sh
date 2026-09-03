#!/bin/sh
for file in *.typ; do
  [ -e "$file" ] || continue
  typst compile "$file" "$1/${file%.typ}.pdf" 
done

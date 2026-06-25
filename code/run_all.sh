#!/usr/bin/env bash

cmake -B build
cmake --build build

# Run all testcases automatically.
mkdir -p output
for scene in testcases/*.txt; do
    name=$(basename "$scene" .txt)
    echo "Rendering $name..."
    build/PA1 "$scene" "output/${name}.bmp" --path
done
echo "Done! All images saved to output/"

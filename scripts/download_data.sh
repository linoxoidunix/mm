#!/bin/bash

set -e

URL="https://github.com/linoxoidunix/mm/releases/download/v0.1.0/data.tar.gz"

echo "Preparing data directory..."
mkdir -p data

echo "Downloading data..."
curl -L $URL -o data.tar.gz

echo "Extracting..."
tar -xzf data.tar.gz -C .

echo "Cleaning..."
rm data.tar.gz

echo "Done."
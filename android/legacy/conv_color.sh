#!/bin/bash

# Wirtten by ChatGPT

# Define the base directory to start the search
BASE_DIR="$1"

# Check if the base directory is provided and exists
if [ -z "$BASE_DIR" ]; then
    echo "Usage: $0 /path/to/directory"
    exit 1
fi

if [ ! -d "$BASE_DIR" ]; then
    echo "Directory $BASE_DIR does not exist."
    exit 1
fi

# Function to process images
process_image() {
    local image_path="$1"
    
    # Define output path (can be modified to save to a different location)
    local output_path="$1" # ${image_path%.*}_tinted.${image_path##*.}"

    # Apply the tint and darkening
    convert "$image_path" \
        -modulate 80,50,55 \
        "$output_path"

    echo "Processed $image_path -> $output_path"
}

export -f process_image

# Find and process image files
find "$BASE_DIR" -type f \( -iname '*.png' \) -exec bash -c 'process_image "$0"' {} \;

echo "Image processing completed."

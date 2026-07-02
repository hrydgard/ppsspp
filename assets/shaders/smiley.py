import struct

# Vibe coded nonsense to initialize a cbuffer

# Constants
WIDTH, HEIGHT = 16, 16
OUTPUT_FILE = "smiley_16x16_rgba.bin"

# Define the colors (RGBA uint32)
# Format: 0xAABBGGRR (Little Endian will write this as R, G, B, A)
YELLOW = 0xFF00FFFF  # Bright Yellow
BLACK  = 0xFF000000  # Opaque Black
TRANS  = 0x00000000  # Fully Transparent

# A simple 16x16 map
# Y = Yellow, B = Black, . = Transparent
smiley_map = [
    "      YYYY      ",
    "    YYYBYYYY    ",
    "   YYYYYYYYYY   ",
    "  YYYYYYYYYYYY  ",
    " YYYBYYYYBBYYY  ",
    " YYYBYYYYBBYYY  ",
    "YYYYYYYYYYYYYYYY",
    "YYYYYYYYYYYYYYYY",
    "YYYYBYYYYYYBYYYY",
    "YYYYYBBYBBBYYYYY",
    " YYYYYYBBBYYYYY ",
    " YYYYYYYYYYYYYY ",
    "  YYYYYYYYYYYY  ",
    "   YYYYYYYYYY   ",
    "    YYYYYYYY    ",
    "      YYYY      ",
]

def generate_binary():
    with open(OUTPUT_FILE, "wb") as f:
        for row in smiley_map:
            for char in row:
                if char == "Y":
                    color = YELLOW
                elif char == "B":
                    color = BLACK
                else:
                    color = TRANS
                
                # 'I' is unsigned int (32-bit), '<' is Little Endian
                f.write(struct.pack('<I', color))

    print(f"Success! Created {OUTPUT_FILE} ({WIDTH}x{HEIGHT} RGBA).")

if __name__ == "__main__":
    generate_binary()

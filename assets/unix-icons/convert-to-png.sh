# Copyright (C) 2014 Sergio Benjamim

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

# Very simple bash script to convert svg to png files, you can choose inkscape or imagemagick ('convert' command) and the path to icons export.
# Only converts assets/unix-icons/icon-512.svg
# Needs improvement, yeah, i know, it is not so good... at least it work :)


#!/bin/bash

# Parameters:
# -s, --software     inkscape | imagemagick    -->  "./convert-to-png.sh -s imagemagick" for example
# -d, --directory    directory path            -->  "./convert-to-png.sh -d /usr/share/icons/hicolor/" or "./convert-to-png.sh -d ../../debian/ppsspp/usr/share/icons/hicolor/" for example

# Default options
software_option="inkscape"      # sometimes imagemagick does not convert very well, so inkscape is default
path="icons"                    # i.e. assets/unix-icons/icons/

echo -e

while [ "$1" != "" ]; do
    case $1 in
        -s | --software )       shift
                                if [ "$1" == "inkscape" ] || [ "$1" == "imagemagick" ]; then
                                    software_option=$1
                                    echo -e "Using $1.\n"
                                else
                                    software_option="inkscape"
                                    echo -e "This parameter does not exist, inkscape or imagemagick are valids parameters. Using Inkscape.\n"
                                fi
                                ;;

        -d | --directory )      shift
                                path=$1
                                path=${path%"/"}
                                ;;

        * )                     echo -e "Error with parameters.\n"
                                exit 1
                                ;;
    esac
    shift
done


# Creating assets/unix-icons/icons/ if user does not choose any directory
if [ "$path" == "icons" ] && [ ! -d "$path" ]; then
    mkdir icons/
fi


# Converting svg icons to png:

# 16 pixel icon use other icon, resize does not fit well for small icons
if [ "$software_option" == "inkscape" ]; then
    if [ "$path" == "icons" ]; then
        inkscape --export-area-page --file=icon-16.svg --export-png=$path/ppsspp_16.png
    else
        inkscape --export-area-page --file=icon-16.svg --export-png=$path/16x16/apps/ppsspp.png
    fi
elif [ "$software_option" == "imagemagick" ]; then
    if [ "$path" == "icons" ]; then
        convert icon-16.svg -transparent white $path/ppsspp_16.png
    else
        convert icon-16.svg -transparent white $path/16x16/apps/ppsspp.png
    fi
fi

x="x"

for size in 24 32 48 64 96 128 256 512
do
    if [ "$software_option" == "inkscape" ]; then
        if [ "$path" == "icons" ]; then
            inkscape --export-area-page --export-width=$size --export-height=$size --file=icon-512.svg --export-png=$path/ppsspp_$size.png
        else
            inkscape --export-area-page --export-width=$size --export-height=$size --file=icon-512.svg --export-png=$path/$size$x$size/apps/ppsspp.png
        fi
    elif [ "$software_option" == "imagemagick" ]; then
        if [ "$path" == "icons" ]; then
            convert icon-512.svg -resize $size -transparent white $path/ppsspp_$size.png
        else
            convert icon-512.svg -resize $size -transparent white $path/$size$x$size/apps/ppsspp.png
        fi
    fi
done

echo -e "\nIcons was exported to $path/ folder.\n"


exit 0

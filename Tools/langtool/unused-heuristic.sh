#!/bin/bash

langfile='../../assets/lang/en_US.ini'
folders=('../../UI/' '../../Core/' '../../Common/' '../../GPU/' '../../Windows/' '../../assets/shaders' '../../assets/themes')

# reading each line
while read line; do
    # skip empty line, section and comment
    if [[ ! -z "$line" && ${line::1} != "[" && ${line::1} != "#" ]]; then
        # get everything before the = sign
        value=${line/ =*/}

        found=0
        for folder in ${folders[@]}; do
            # use recursive grep on the folder
            grep -r "$value" $folder > /dev/null
            # check return value
            ret=$?
            if [ $ret -eq 0 ]; then
                found=1
            fi
        done

        # print if not found
        if [ $found -eq 0 ]; then
            echo $value
        fi
    fi
done < $langfile
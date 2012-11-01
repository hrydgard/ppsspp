# AWFUL way to make the shaders macosx compatible!

cd assets/shaders
for i in *.vs *.fs
do
  sed -i '' s/lowp\ //g $i
  sed -i '' s/highp\ //g $i
  sed -i '' s/mediump\ //g $i
  sed -i '' s/#version\ 140//g $i
done
cd ../..

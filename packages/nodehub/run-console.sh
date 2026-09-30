#sh scripts/build.sh Debug console
sh scripts/build.sh
cd build/bin
./nodehub_d.exe --graph=./BlueprintsGraph.json --run --headless --log-level=debug --max-steps=1000 --log=./last-run.log


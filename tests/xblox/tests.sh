pm-image-cli.exe xblox --log-level info run --src tests\xblox\while-count.xblox
pm-image-cli.exe xblox --log-level info run --src tests\xblox\fps-unthrottled.xblox
pm-image-cli.exe xblox --log-level info run --src tests\xblox\fps-raw-loop.xblox
node tests\xblox\fps-unthrottled-node.mjs
node tests\xblox\fps-raw-loop-node.mjs 6
python tests\xblox\fps-unthrottled-python.py
python tests\xblox\fps-raw-loop-python.py 6
rustc -O tests\xblox\fps-unthrottled-rust.rs -o tests\xblox\fps-unthrottled-rust.exe && tests\xblox\fps-unthrottled-rust.exe || echo rustc not available
rustc -O tests\xblox\fps-raw-loop-rust.rs -o tests\xblox\fps-raw-loop-rust.exe && tests\xblox\fps-raw-loop-rust.exe 6 || echo rustc not available

cpp     422603
python  456895.9
rust    6137224.6
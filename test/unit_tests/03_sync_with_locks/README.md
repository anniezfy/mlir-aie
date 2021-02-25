To build design, simply type:
```
make 
```
Then you can copy the entire 03_sync_with_locks diretory to the board and then compile the arm code on the board and run the test.
```
scp -r 03_sync_with_locks xilinx@<board ip>:/home/xilinx/.
```

On the board, execute:
```
cd 03_sync_with_locks
cp acdc_project/aie_inc.cpp .
cp acdc_project/core*elf .
make test.exe
sudo ./test.exe
```

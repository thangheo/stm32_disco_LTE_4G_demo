1. Link to STM32F4 discovery board  :https://www.st.com/en/evaluation-tools/stm32f4discovery.html
2. To build on Linux ,use either 1 of these 2 option :
a. sudo apt install gcc-arm-none-eabi -y
b. First download gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 from this link
 https://developer.arm.com/downloads/-/gnu-rm
then extract to /usr/bin/ 
3.After finnish install compiler, just cd into source code directory and type "make" in terminal.
The output .bin and .elf should be there in ./build directory
4. you can flash to your discovery board using the flash.sh script after connect the board to your computer
5. The A7670 4G LTE module is connected to UART2 
6. UART3 is used for printf/debugging (baudrate 115200), use minicom and connect the USB-uart carefully to your board.
remember stm32 uart voltage level is 3.3. Use a proper level converter or a 3.3v usb-uart
7. I use a cheap local A7670C Cat-1 speed module with 4 pins output include 5V,Tx,Rx,GND. You can use module's reset pin to
reset , the module looks like this https://www.electrodragon.com/product/a7670-lte-cat-1-gsm-mini-module/
I dont guarantee it would works with other LTE module but in theory LTE chip vendor should share common command for pppos protocol.
8. This is an IN PROGRESS because my LTE module is damage from testing with a lot of stuff and I havent buy the new one.
# stm32_disco_LTE_4G_demo

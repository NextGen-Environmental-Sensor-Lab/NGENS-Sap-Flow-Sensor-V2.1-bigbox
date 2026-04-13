**Sap flow sensor V2.1 ‘bigbox’: Setup Procedure** 

**Before you start get the following ready:**

1. **Arduino IDE** with the Adafruit Feather **RP2040 logger board selecte**d.

2. Recently downloaded Arduino ‘**sap\_flow2.ino’ code from [github](https://github.com/NextGen-Environmental-Sensor-Lab/NGENS-Sap-Flow-Sensor-V2.1-bigbox)** (github.com/NextGen-Environmental-Sensor-Lab/NGENS-Sap-Flow-Sensor-V2.1-bigbox)

3. Micro **SD card reade**r. “SD card formatter” app.

4. **Special ‘switched’ USB-C cable (home made) and the USB-C 90degree adapter.**

5. **SFS box** with the **coin battery** in place, microcontroller card and an **SD card i**n the microcontroller and a **battery cable**.

6. A **charged battery pack (12V)** disconnected from board.

7. **SFS probe with a number** on it (means it’s been tested)

8. **Beaker with ice and water mix**

**Procedure to set up the board:**

1. Open the box and **remove the SD** card. **Disconnect battery power** (the red-black cable) from the board.

2. Put the **SD card in the SD card reade**r and check that it is OK. If not showing up, reformat with the ‘SD card formatter’ app.

3. Put **SD back into board, but not all the way \- don’t click it in.**

4. **Connect L-90degree usb-c** adapter and **custom USB-C cable** with **the switch off** (away from end)

5. **Connect SFS probe,** if not connected already. 

6. Open the Arduino IDE and **open the sap\_flow2.ino.** Select the “Adafruit Feather RP2040. Run the compiler once (check mark upper left icon) to make sure all libraries are in place

7. Turn the **system on from the USB cable switch**.

8. Select the **port at Tools\>Port\>/dev/cu.usb(Adafruit …**) (on Mac) and then **open the serial monitor** (spyglass icon upper right). You may get some output on the screen.

9. **If you do not see the /dev/cu.usb(Adafruit..) port go to debug at the end**

10. **Upload code (**right arrow upper left corner). When finished **open the serial monitor**. If you don’t see output, **reset the board** (little black button on microcontroller board next to the USB-C connector). You should get output on the serial monitor that ends on ‘Card failed or not present\!’ and the **Error LED blinking**.

11. Put the 3x **probe needles into the ice water**, **plug the battery power in**, c**lick the SD card in**, and click the **reset button again.** This starts a measurement cycle and you will see the measurements on the screen. While running **turn the USB cable power off**. The process will continue and conclude because it is now on battery power. 

12. It will restart on the hour and on the half-hour. If you want to get a second reading of ice-water press the **black Wake button on the main board and hold it until the green LED flashes**. This will force another measurement cycle.

At this point the system is saving the ice-water temperature values in a file called UNKNOWN. It may be of use later to calibrate the thermistors.

13. **Provisioning (enter date, time and board ID).** **Click-out the SD card** and force a cycle with the **Wake button (hold until green LED blinks**). Because the SD card is out it will go to the blinking Error.

14. To enter provisioning **click the Aux black button on the corner of the main board.** It will flash all the LEDs a few times and you will see output on the Serial monitor indicating PROVISIONING MODE. We have two minutes to do this before it goes back to the error cycle.

15. At the top of the Serial Monitor window **enter the information: YYYY/MM/DD HH:MM:SS DEVICEID. I have been using the format SFS0xx for DEVICEID with xx corresponding to the number on the probe.** 

16. **Synchronizing Tip:** Choose a **MM:SS a bit in the future, 30s** or so, and **enter**. It will ask for a confirmation y or Y. **Type y or Y but don’t enter until the computer clock and the entered time coincide.** This will save the info to chip  memory and exit back to the error cycle. If you want to make sure the data is correct you can re-enter provisioning (Aux). The saved values will print out on the serial monitor. If correct click Aux again and this will exit the process.

17. **Add label to board** with orange tape with DEVICEID. **Disconnect battery** power cable. **READY**. 

**DEBUG: Serial port missing** 

If the Serial port is not showing up: **press ‘Boot’** black button, then **click ‘Reboot’** black button, then **release ‘Boot’** black button. This will make a **strange UF2 Board** appear int the **Tools\>Port** on the Arduino IDE that will allow you to upload the code. When upload done, go back to **Tools\>Port** and the normal port name should be there called  /dev/cu.usbmodem1101 (Adafruit Feather RP2040 Adalogger).
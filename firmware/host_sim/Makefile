# Host build of the stand firmware with the simulated board (bsp_sim.c)
APP  = ../stm32h723/Core/Src/app
INC  = ../stm32h723/Core/Inc
SRC  = bsp_sim.c $(wildcard $(APP)/*.c)
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I$(INC)
stand_sim: $(SRC) $(wildcard $(INC)/app/*.h)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm
clean:
	rm -f stand_sim sim_eeprom.bin

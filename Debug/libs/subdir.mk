################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../libs/circular_queue.cpp \
../libs/pool.cpp 

OBJS += \
./libs/circular_queue.o \
./libs/pool.o 

CPP_DEPS += \
./libs/circular_queue.d \
./libs/pool.d 


# Each subdirectory must supply rules for building sources it contributes
libs/%.o: ../libs/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -I"/home/hemiao/workspace/video/include" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '



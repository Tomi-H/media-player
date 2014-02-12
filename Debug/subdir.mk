################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../circular_queue.cpp \
../main.cpp \
../packet_queue.cpp \
../pool.cpp 

OBJS += \
./circular_queue.o \
./main.o \
./packet_queue.o \
./pool.o 

CPP_DEPS += \
./circular_queue.d \
./main.d \
./packet_queue.d \
./pool.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -I/home/hemiao/workspace/video/video/include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '



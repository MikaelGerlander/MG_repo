/*
 * Projekti_1.c
 *
 * The program controls the frequency of a buzzer by reading the ADC input of a potentiometer.
 * The sound frequency is from 50-1000Hz and it is produced with PWM signal. 
 * Frequency is displayed to user via USART.
 * System enters to sleep mode when PD2 is pressed.
 *
 * Created: 27.11.2024 11.38.25
 * Author : Mikael Gerlander
 */ 

#include <avr/io.h>
#define F_CPU 16000000
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <string.h> // For string handling
#include <stdio.h>

// Initialize global variable for frequency
uint16_t frequency_control = 283;

// Constants for the baud rate generation
#define BAUD 9600        // Baud rate
#define MYUBRR (F_CPU/16/BAUD-1) // USART Baud Rate Register

// Variable for storing adc result
volatile uint16_t conversion_result;

// Constant messages to virtual terminal
const char *msg = "Frequency is ";
const char *end_msg = " Hz\r\n";


// Define the task numbers for LED and frequency input for virtual terminal and maximum number of tasks
#define TASK_MAX 2
#define ADC_TASK 1
#define FREQ_TASK 2

// Initialize a struct for time triggered tasks
struct{
	uint8_t task_number;
	int16_t delay;
	uint16_t interval;
	int8_t run;
}task_list[TASK_MAX];



void setup_system(void){
	// Initializes timers, pins and pwm modes that the system uses
	
	// Set led pin as output
	DDRD |= (1<<PD5);
	DDRD &= ~(1<<PD2);
	// Port C consists only input pins. PC0 (ADC), PC6 (reset)
	DDRC = 0x00;
	// Set the sounder pin as output
	DDRB |= (1<<PB1);
	
	// Setting the time triggered program
	// timer 2
	TCCR2B |= (1<<CS21);				// normal mode, prescaler 8
	TIMSK2 |= (1<<TOIE2);				// Timer overflow interrupt enable
	
	
	PCICR |= (1<<PCIE2);
	PCMSK2 |= (1<<PCINT18);	// Enable PCINT in PC3
	
	
	// Initialize fast pwm for the led
	TCCR0A |= (1<<WGM01);				// Set the mode to CTC
	TCCR0A |= (1<<COM0B0);				// Toggle 0C0B 
	TCCR0B |= (1<<CS02);	// Set prescaler to 256
	OCR0A = 200;
	
	
	// Initialize fast pwm for the sounder
	TCCR1A |= (1 << WGM11) | (1 << WGM10) | (1 << COM1A1); // Fast PWM mode
	TCCR1B |= (1 << CS11) | (1<<CS10);  // Set prescaler to 64
	
	// The wave form is defined by equation F /(2*N*(1+OCR1A))
	// So if we use 64 prescaler and 16MHz and solve the OCR1A we get
	// 16MHz / (2*64*f)-1
	// So the min and max values are
	// OCR1A = 16MHz / (2*64*1000)-1 = 124
	// OCR1A = 16MHz / (2*64*50)-1 = 2499
	
	// Set for 283, so that the starting frequency is 440Hz
	OCR1A = frequency_control;  // Modify this value to adjust the PWM frequency 
	
	
	// init AD channels
	// disable channel input buffers
	DIDR0 = 0x01;
	
	// Use Avcc as reference voltage
	ADMUX |= (1<<REFS0);
	
	// use 10 bits -> right aligned data ADCSRA[ADLAR] -> 0
	ADMUX &= ~(1<<ADLAR); // reset bit
	
	// clock prescaler 128, ADCSRA [ADPS 2:0] -> 0x111
	ADCSRA |= (1<<ADPS2) | (1<<ADPS1) | (1<<ADPS0);
	
	// Enable interrupts in AD
	ADCSRA |= (1<<ADIE);
	
	// Enable AD converter
	ADCSRA |= (1<<ADEN);
	
	
	// Initialize usart
	// Set baud rate
	UBRR0L = (unsigned char)MYUBRR;         // Low byte
	UBRR0H = (unsigned char)(MYUBRR >> 8);  // High byte

	// Enable receiver and transmitter
	UCSR0B |= (1 << RXEN0) | (1 << TXEN0);

	// Set frame format: 8 data bits, 1 stop bit, no parity
	UCSR0C |= (1 << UCSZ01) | (1 << UCSZ00);
	
	
	
	
	sei();	// enable interrupts;
	
}



void update(){
	// Function is called when tick happens and it checks if it is time to run tasks
	// If yes, it changes the run bit to 1
	// It also checks if the task is periodic, and sets delay
	
	int index;
	for(index = 0; index < TASK_MAX; index++){
		// Checks that there is a task in that index
		if(task_list[index].task_number != 0){
			if(task_list[index].delay == 0){
				task_list[index].run += 1;	// Sets task to be run
				if(task_list[index].interval != 0){
					task_list[index].delay = task_list[index].interval;	// Sets the delay
				}
			}
			// If it is not time yet for the task, reduces delay by 1 tick
			else{
				task_list[index].delay -= 1;
			}
		}
	}
}



// Transmits strings with polling
void send_string_direct(const char *str) {
	while(*str) {
		
		while (!(UCSR0A & (1 << UDRE0)));
		// Put data in the UDR0
		UDR0 = *str++;
	}
}



#define OVF_MAX 156	// With 8-bit timer with a prescaler of 8, 156 gives 20ms tick
uint16_t overflows=0;

ISR (TIMER2_OVF_vect) {
	if(overflows++ >= OVF_MAX) {
		update();
		overflows = 0;
	}
}



void add_task(uint8_t number, uint8_t task_number, int16_t delay, int16_t interval){
	// Function that adds tasks to the struct	
	task_list[number].task_number = task_number;
	task_list[number].delay = delay;
	task_list[number].interval = interval;
}



void init_tasks(){
	// Initializes task and gives them number, delay, and how often they are repeated(interval)
	int index;
	for(index = 0; index < TASK_MAX; index++){
		task_list[index].task_number = 0;
		
		add_task(0, ADC_TASK, 0, 100);		// Reads adc 5 times a second
		add_task(1, FREQ_TASK, 50, 100);		// Sends text to virtual terminal every 2s
	}
}



void task_1(){
	// Does the ADC
	// Select ADC0 to read
	ADMUX = (ADMUX & 0xF0) | (0x00 & 0x0F);
	
	// start conversion by setting ADCSRA[ADSC] -> 1
	ADCSRA |= (1<<ADSC);
}



void task_2(){
	// Function gives commands to send strings to virtual terminal
	// Also, converts adc result from int to string
	
	send_string_direct(msg); // Send the constant string
	
	// Converts the result from int to char array
	char str[10];
	int i = 0;

	// Convert number to string (in reverse order)
	do {
		str[i++] = (conversion_result % 10) + '0';
		conversion_result /= 10;
	} while (conversion_result > 0);

	// Null-terminate the string
	str[i] = '\0';

	// Reverse the string
	for (int j = 0, k = i - 1; j < k; j++, k--) {
		char temp = str[j];
		str[j] = str[k];
		str[k] = temp;
	}
	
	send_string_direct(str); // Send the string one character at a time
	send_string_direct(end_msg); // Send the string one character at a time
}



void task_manager(){
	// Checks the run bits from every task, and runs them if needed
	int index;
	for(index = 0; index < TASK_MAX; index++){
		// Checks that there is a task in that index
		if(task_list[index].task_number != 0){
			if(task_list[index].run > 0){
				
				if(task_list[index].task_number == ADC_TASK) task_1();
				else if(task_list[index].task_number == FREQ_TASK) task_2();
				task_list[index].run -= 1;
			}
		}
	}
}



void go_sleep(){
	// Sets system to sleep on standby mode
	set_sleep_mode(SLEEP_MODE_STANDBY);
	cli();
	// Turn of the led
	
	sleep_enable();	// Sets SE-bit
	sei();			// Enable interrupts
	sleep_cpu();	// Sleep-instruction
	
	// When the button is released the system starts again
	sleep_disable();
	
	// The led lights automatically after system wakes up
}



ISR(PCINT2_vect){
	
	// If the PD2 is high, the system wakes up automatically
}



ISR(ADC_vect){
	// Reads the adc and saves it in a variable
	conversion_result = ADC;
	
	// Making sure that the result stays in wanted zone 50-1000Hz
	if(conversion_result < 50){
		conversion_result = 50;
	}
	else if( conversion_result > 1000){
		conversion_result = 1000;
	}
	
	// Sets also sounder frequency
	frequency_control = 124 + ((2499-124)*conversion_result) / 1000;
	OCR1A = frequency_control;
}






int main(void)
{
    init_tasks();
	setup_system();
	
    while (1){
		
		// Call the task manager to see if there are any tasks to run
		task_manager();
		
		// If pin is low, goes to sleep
		if(!(PIND & (1<<PD2))){
			go_sleep();
		}
    }
}


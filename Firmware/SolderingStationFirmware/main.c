/*
 * FANS.c
 *
 * Created: 22.07.2016 12:44:43
 * Author : Администратор
 */ 


# define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/sfr_defs.h>
#include "hd44780.h"


#define   SetBit(reg, bit)          reg |= (1<<bit)
#define   ClearBit(reg, bit)       reg &= (~(1<<bit))
#define   InvBit(reg, bit)          reg ^= (1<<bit)
#define   BitIsSet(reg, bit)       ((reg & (1<<bit)) != 0)
#define   BitIsClear(reg, bit)    ((reg & (1<<bit)) == 0)

// Voltage Reference: AVCC pin
#define ADC_VREF_TYPE ((0<<REFS1) | (1<<REFS0) | (0<<ADLAR))

int CurrenFanTemperature;
//int CurrentSoldTemperature;
int UserFanSpeed;
int UserFanTemperature;
int NeedFanTemperature;
//int UserSoldTemperature;

int test = 0;
int Gerkon_mode = 0;
int FanSleep = 0;
int time = 0;
int min;
int sec;

#define SAMPLING 32

int temperatures[SAMPLING];
int curTempSample;

#define HEATER_SLEEP (Gerkon_mode > 0)

#define GERKON_TIMING 5 // таймин переключения геркона
#define SLEEP_FAN_TEMPERATURE 60 // температура до которой астужать на подставке
#define MIN_OFF 200//ADC вентилятора при котором отключать полностью нагреватель

unsigned char bar0[] = {31,4,0,0,0,4,31,0};
unsigned char bar1[] = {31,20,16,16,16,20,31,0};
unsigned char bar2[] = {31,28,24,24,24,28,31,0};
unsigned char bar3[] = {31,28,28,28,28,28,31,0};
unsigned char bar4[] = {31,30,30,30,30,30,31,0};
unsigned char bar5[] = {31,31,31,31,31,31,31,0};
	
	
// Read the AD conversion result
unsigned int read_adc(unsigned char adc_input)
{
	ADMUX=adc_input | ADC_VREF_TYPE;
	// Delay needed for the stabilization of the ADC input voltage
	_delay_us(10);
	// Start the AD conversion
	ADCSRA|=(1<<ADSC);
	// Wait for the AD conversion to complete
	while ((ADCSRA & (1<<ADIF))==0);
	ADCSRA|=(1<<ADIF);
	return ADCW;
}

ISR(TIMER1_OVF_vect)
{
	
	// Reinitialize Timer1 value
	TCNT1H=0xBDC >> 8;
	TCNT1L=0xBDC & 0xff;
	// Place your code here
	if (UserFanSpeed > MIN_OFF)
	{
		time++;
		min = time/60;
		sec = time-(min*60);
	}
	else
	{
		time=0;min=0;sec=0;
	}
}

char move;

void DrawProgress(int value, int maxValue, int chartercount)
{
	int d;
	int dd;
	d = maxValue/(chartercount*5);
	dd = maxValue/chartercount;
	for (int i = 0; i < chartercount; i++)
	{
		int c = value - i*dd;
		if (c>=d*5) lcd_putc(5);
		else
		if (c>=d*4) lcd_putc(4);
		else		
		if (c>=d*3) lcd_putc(3);
		else
		if (c>=d*2) lcd_putc(2);
		else
		if (c>=d) lcd_putc(1);
		else
		lcd_putc(0);
	}
}

int main(void)
{
	
	// Input/Output Ports initialization
	// Port B initialization
	// Function: Bit7=In Bit6=In Bit5=In Bit4=In Bit3=In Bit2=Out Bit1=Out Bit0=Out
	DDRB=(0<<DDB7) | (0<<DDB6) | (0<<DDB5) | (0<<DDB4) | (1<<DDB3) | (1<<DDB2) | (1<<DDB1) | (1<<DDB0);
	// State: Bit7=T Bit6=T Bit5=T Bit4=T Bit3=T Bit2=T Bit1=0 Bit0=0
	PORTB=(0<<PORTB7) | (0<<PORTB6) | (0<<PORTB5) | (0<<PORTB4) | (0<<PORTB3) | (0<<PORTB2) | (0<<PORTB1) | (0<<PORTB0);

	// Port C initialization
	// Function: Bit6=In Bit5=In Bit4=In Bit3=In Bit2=In Bit1=In Bit0=In
	DDRC=(0<<DDC6) | (0<<DDC5) | (0<<DDC4) | (0<<DDC3) | (0<<DDC2) | (0<<DDC1) | (0<<DDC0);
	// State: Bit6=T Bit5=T Bit4=T Bit3=T Bit2=T Bit1=T Bit0=T
	PORTC=(0<<PORTC6) | (0<<PORTC5) | (0<<PORTC4) | (0<<PORTC3) | (1<<PORTC2) | (0<<PORTC1) | (0<<PORTC0);

	// Port D initialization
	// Function: Bit7=Out Bit6=Out Bit5=Out Bit4=Out Bit3=Out Bit2=In Bit1=In Bit0=In
	DDRD=(1<<DDD7) | (1<<DDD6) | (1<<DDD5) | (1<<DDD4) | (1<<DDD3) | (0<<DDD2) | (0<<DDD1) | (0<<DDD0);
	// State: Bit7=0 Bit6=0 Bit5=0 Bit4=0 Bit3=0 Bit2=T Bit1=T Bit0=T
	PORTD=(0<<PORTD7) | (0<<PORTD6) | (0<<PORTD5) | (0<<PORTD4) | (0<<PORTD3) | (0<<PORTD2) | (0<<PORTD1) | (0<<PORTD0);

	
	// Timer/Counter 0 initialization
	// Clock source: System Clock
	// Clock value: Timer 0 Stopped
	// Mode: Normal top=0xFF
	// OC0A output: Disconnected
	// OC0B output: Disconnected
	TCCR0A=(0<<COM0A1) | (0<<COM0A0) | (0<<COM0B1) | (0<<COM0B0) | (0<<WGM01) | (0<<WGM00);
	TCCR0B=(0<<WGM02) | (0<<CS02) | (0<<CS01) | (0<<CS00);
	TCNT0=0x00;
	OCR0A=0x00;
	OCR0B=0x00;
	
	// Timer/Counter 1 initialization
	// Clock source: System Clock
	// Clock value: 62,500 kHz
	// Mode: Normal top=0xFFFF
	// OC1A output: Disconnected
	// OC1B output: Disconnected
	// Noise Canceler: Off
	// Input Capture on Falling Edge
	// Timer Period: 1 s
	// Timer1 Overflow Interrupt: On
	// Input Capture Interrupt: Off
	// Compare A Match Interrupt: Off
	// Compare B Match Interrupt: Off
	TCCR1A=(0<<COM1A1) | (0<<COM1A0) | (0<<COM1B1) | (0<<COM1B0) | (0<<WGM11) | (0<<WGM10);
	TCCR1B=(0<<ICNC1) | (0<<ICES1) | (0<<WGM13) | (0<<WGM12) | (1<<CS12) | (0<<CS11) | (0<<CS10);
	TCNT1H=0x0B;
	TCNT1L=0xDC;
	ICR1H=0x00;
	ICR1L=0x00;
	OCR1AH=0x00;
	OCR1AL=0x00;
	OCR1BH=0x00;
	OCR1BL=0x00;
	
	// Timer/Counter 2 initialization
	// Clock source: System Clock
	// Clock value: 16000,000 kHz
	// Mode: Fast PWM top=0xFF
	// OC2A output: Non-Inverted PWM
	// OC2B output: Disconnected
	// Timer Period: 0,016 ms
	// Output Pulse(s):
	// OC2A Period: 0,016 ms Width: 0 us
	ASSR=(0<<EXCLK) | (0<<AS2);
	TCCR2A=(1<<COM2A1) | (0<<COM2A0) | (0<<COM2B1) | (0<<COM2B0) | (1<<WGM21) | (1<<WGM20);
	TCCR2B=(0<<WGM22) | (0<<CS22) | (0<<CS21) | (1<<CS20);
	TCNT2=0x00;
	OCR2A=0x00;
	OCR2B=0x00;
	
	// Timer/Counter 0 Interrupt(s) initialization
	TIMSK0=(0<<OCIE0B) | (0<<OCIE0A) | (0<<TOIE0);

	// Timer/Counter 1 Interrupt(s) initialization
	TIMSK1=(0<<ICIE1) | (0<<OCIE1B) | (0<<OCIE1A) | (1<<TOIE1);

	// Timer/Counter 2 Interrupt(s) initialization
	TIMSK2=(0<<OCIE2B) | (0<<OCIE2A) | (0<<TOIE2);

	// ADC initialization
	// ADC Clock frequency: 1000,000 kHz
	// ADC Voltage Reference: AVCC pin
	// ADC Auto Trigger Source: ADC Stopped
	// Digital input buffers on ADC0: Off, ADC1: Off, ADC2: Off, ADC3: On
	// ADC4: Off, ADC5: Off
	DIDR0=(1<<ADC5D) | (1<<ADC4D) | (0<<ADC3D) | (1<<ADC2D) | (1<<ADC1D) | (1<<ADC0D);
	ADMUX=ADC_VREF_TYPE;
	ADCSRA=(1<<ADEN) | (0<<ADSC) | (0<<ADATE) | (0<<ADIF) | (0<<ADIE) | (1<<ADPS2) | (0<<ADPS1) | (0<<ADPS0);
	ADCSRB=(0<<ADTS2) | (0<<ADTS1) | (0<<ADTS0);
	
	ClearBit(PORTB, PORTB2);
	
	lcd_init();
	lcd_loadchar(bar0, 0);
	lcd_loadchar(bar1, 1);
	lcd_loadchar(bar2, 2);
	lcd_loadchar(bar3, 3);
	lcd_loadchar(bar4, 4);
	lcd_loadchar(bar5, 5);
	
	sei();
	curTempSample = 0;
    /* Replace with your application code */
    while (1) 
    {
		UserFanSpeed = read_adc(1);
		UserFanTemperature = read_adc(0)*0.5;
		//temperatures[curTempSample] = read_adc(4);
		//curTempSample++;
		//if (curTempSample >= SAMPLING)
		//{
		//	CurrenFanTemperature=0;
		//	for (int i = 0; i<SAMPLING;i++)
		//		CurrenFanTemperature += temperatures[curTempSample];
		//	CurrenFanTemperature = CurrenFanTemperature/SAMPLING;
		//	curTempSample = 0;
		//}
		if (BitIsSet(PORTB, PORTB2))
		{
			ClearBit(PORTB, PORTB2);
			CurrenFanTemperature = read_adc(4);
			SetBit(PORTB, PORTB2);
		} else CurrenFanTemperature = read_adc(4);
		
		CurrenFanTemperature = 0.72*CurrenFanTemperature+56.5;

		if (UserFanSpeed < MIN_OFF)//если скорость вентилятора ниже минимума
		{
			ClearBit(PORTB, PORTB2);//выключаем нагреватель
			if (CurrenFanTemperature > SLEEP_FAN_TEMPERATURE)// если температура выше минимума то продувка на максимуме
				OCR2A = 0xFF;
			else
				OCR2A = 0x00;//иначе выключаем вентилятор
		}
		else
		{
			
			if (BitIsClear(PIND, PIND2))
			{
				if (Gerkon_mode < GERKON_TIMING) Gerkon_mode++;
			}
			else
			{
				if (Gerkon_mode > -GERKON_TIMING) Gerkon_mode--;
			}
			
			if HEATER_SLEEP
			NeedFanTemperature = SLEEP_FAN_TEMPERATURE;
			else
			NeedFanTemperature = UserFanTemperature;
			
			NeedFanTemperature = UserFanTemperature;

			if (NeedFanTemperature < 50)
			{
				ClearBit(PORTB, PORTB2);
			}
			else
			{
				if (CurrenFanTemperature < NeedFanTemperature)
					SetBit(PORTB, PORTB2);
				else
					ClearBit(PORTB, PORTB2);
			}
			
		}
		
		lcd_goto(LCD_1st_LINE,0);
		lcd_itos(UserFanTemperature);
		lcd_puts("C  R:");
		lcd_itos(CurrenFanTemperature);
		lcd_puts("C T:");
		lcd_itos(min);
		lcd_puts(":");
		lcd_itos(sec);
		lcd_puts(" ");
		lcd_goto(LCD_2nd_LINE,0);
		if (UserFanSpeed < MIN_OFF)
		{
			lcd_puts("OFF");
		}
		else
		{
			int speed = (UserFanSpeed-MIN_OFF)/8.24;
			lcd_itos(speed);
			lcd_puts("% ");
			test = speed*1.28 + 128;
			if (OCR2A != test)
			{
				OCR2A = test;
			}
		}
		lcd_goto(LCD_2nd_LINE,4);
		if (OCR2A < 128)
			DrawProgress(0, 128, 10);
		else
			DrawProgress(OCR2A-128, 128, 10);
    }
}


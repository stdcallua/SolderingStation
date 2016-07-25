#define F_CPU 8000000UL
#include <avr/io.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdint.h>


//���� ����
#define Kp 1
#define Ki 5
#define Kd 10

#define FAN_TIMEOUT 6000//������� ���������� ������� (��/10)
#define TIME_LIMIT 100 //������� �� �������� ������ 
#define LIFETIME_LIMIT 300000 //������� �� �������� ������ (��/10)

#define POWER_SW (PIND>>7)  //������ �������\������������ ������ ���������
#define FAN_SW (PINC&(1<<2))>>2 //������ ��������� ����

#define SET_SW1_OUT 1 //����� ���� ������
#define SET_SW1 ((PINB&(1<<6))>>6)&(PINB&1) //��������� ������� 1

#define SET_SW2_OUT 2 //����� ���� ������
#define SET_SW2 ((PINB&(1<<5))>>5)&(PINB&1) //��������� ������� 2

#define FAN_UP_SW_OUT 0 //����� ���� ������
#define FAN_UP_SW ((PINB&(1<<7))>>7)&(PINB&1) // ���+

#define FAN_DOWN_SW_OUT 3 //����� ���� ������
#define FAN_DOWN_SW ((PINB&(1<<4))>>4)&(PINB&1) // ���-



#define ENC_STATE ((PINC&0b00110000)>>4) //��������� ��������
#define PWM_FAN(x) PORTB=(PORTB&0x11110111)|(x<<3) //����������� ����

#define LED_IN(x) PORTD=(PORTD&0b10000000)|(x&0b01111111) // �������� �������
#define LED_OUT(x) PORTB=(PORTB&0b00001111)|(0b10000000>>x) //��������� �������� ���� �������\�����





unsigned char LedDisplaySegments[14]={ // 0 1 2 3 4 5 6 7 8 9 E r - f
0b0111111,0b0000110,0b1011011,
0b1001111,0b1100110,0b1101101,
0b1111101,0b0000111,0b1111111,
0b1101111,0b1111001,0b1010000,
0b1000000,0b1110001}; 


int display;

unsigned char Command; //��� ������� �������
//1-temp up 
//2-temp down 
//3-fan up 
//4-fan down 
//5-load1 
//6-load2 
//7-save1 
//8-save2 
//9-ch.dev dtybr
//10-power 
//11-fan on\off
//12-fan active
	

unsigned int TimeKeyDown[6]; //����� ��������� ������
unsigned char LastEncState; //������� ��������� ��������

unsigned char CurrentDev; //�������� ������ ���\��������
unsigned char CurrentADC; //�������� ���
unsigned char CurrentChannel; // ����� �������� �������� ���� �������\�����
unsigned char CurrentDisplayData[3]; //��� �������� �� ������ 

signed int CurrentFanTemp; //����������� ���� (��)
signed int CurrentCopperTemp; // ����������� ��������� (��)
//signed int LastCurrentCopperTemp;// ������� ����������� ��������� (��)
//signed int TLastCurrentCopperTemp;// ������� ����������� ��������� (��)

signed int CurrentCopperPWM; //��� ���������

unsigned int FanSpeed; //������� �������� ����
unsigned int FanTemp; //������� ����������� ���� (��)
signed int CopperTemp; //������� ����������� ��������� (��)



unsigned char PowerState; // ������� ���\����
unsigned char FanPowerState; //������� ���� 0-���� 1-��� 2-���
unsigned int FanPowerTimer; //����� ������ ����
double Lifetime; //����� ������ ��� ��������

unsigned int SourceValueShowTimer; //������ ������ �������� ��������
unsigned int EEMEM_TimeRefresh; //������� ����������

unsigned int EEMEM stored_data[3][3]; //������ ��������



void Init(){
	PowerState=0;

	LastEncState=ENC_STATE; //���� ��������

	//�������� �������� � �������� ���������
	CopperTemp=eeprom_read_word(&stored_data[0][0]);
	FanTemp=eeprom_read_word(&stored_data[1][0]);
	FanSpeed=eeprom_read_word(&stored_data[2][0]);

	if ((CopperTemp >350)||(CopperTemp<50)) CopperTemp=100;
	if ((FanTemp >500)||(FanTemp<50)) FanTemp=100;
	if ((FanSpeed >7)||(FanSpeed<1))  FanSpeed=1;


	//���� ������
	DDRD= 0b01111111; 
	PORTD=0b10000000;

	DDRC= 0b00000000;
	PORTC=0b00110100;

	DDRB= 0b11111110;
	PORTB=0b00000001;


	// ���� ���
	ADCSRA=0b10001010; 

	ADMUX =0b11000000;

	//���� ������ 0
	TCCR0|=0b010;//�������� �� 64
	TIMSK|=1<<TOIE0; //���������� ���
	
	//���� ��� 10��� 
	TCCR1A=0b10100010; 
	TCCR1B=0b00010010;
	ICR1=1023;








};

unsigned char EncoderScan() //������ ��������
{

	char Com; 
 	char New;

 	New=ENC_STATE;
	Com=0;

	switch(LastEncState)
	{
	case 2:
		{
		if(New == 3) Com=1;
		if(New == 0) Com=2; 
		break;
		}
 
	case 0:
		{
		if(New == 2) Com=1;
		if(New == 1) Com=2;  
		break;
		}

	case 1:
		{
		if(New == 0) Com=1;
		if(New == 3) Com=2;  
		break;
		}

	case 3:
		{
		if(New == 1) Com=1;
		if(New == 2) Com=2;  
		break;
		}
	}

	LastEncState = New;		
	return  Com;
};



void Command_Check(char LCommand){
	
	if (LCommand>0) {
		SourceValueShowTimer=TIME_LIMIT;
		Lifetime=0;
	};

	if ((PowerState==0)&&(LCommand!=10)) return; //���� ������� ��������� - ���� ������ ���������

	switch (LCommand){//�������� �� �������
		case 0:
		{
		break;
		};

		case 1: //1-temp up 
		{
			if (CurrentDev==1) { //�������� �������� ��� ���
				if (FanTemp<450) FanTemp+=5;
				else FanTemp=450;

			}
			else {
				if (CopperTemp<350) CopperTemp+=5;
				else CopperTemp=350;
			};
		break;
		};


		case 2://2-temp down 
		{
			if (CurrentDev==1) { //�������� �������� ��� ���
				if (FanTemp>80) FanTemp-=5;
				else FanTemp=80;
			}
			else {
				if (CopperTemp>50) CopperTemp-=5;
				else CopperTemp=50;
			};
		break;


		};
		case 3://3-fan up 
		{
			if (FanSpeed<7) FanSpeed++;
			CurrentDev=1;

		break;
		};
	
	
		case 4://4-fan down 
		{
			if (FanSpeed>1) FanSpeed--;
			CurrentDev=1;
		break;
		};


		case 5://5-load1 
		{
			if (CurrentDev==1) { //�������� �������� ��� ���
				FanTemp=eeprom_read_word(&stored_data[1][1]);
				FanSpeed=eeprom_read_word(&stored_data[2][1]);
			}
			else{
				CopperTemp=eeprom_read_word(&stored_data[0][1]);
			};
			
		break;
		};
		case 6://6-load2 
		{
			if (CurrentDev==1) { //�������� �������� ��� ���
				FanTemp=eeprom_read_word(&stored_data[1][2]);
				FanSpeed=eeprom_read_word(&stored_data[2][2]);
			}
			else{
				CopperTemp=eeprom_read_word(&stored_data[0][2]);
			};

			
		break;
		};
	
	
		case 7://7-save1 
		{
			if (CurrentDev==1) { //�������� �������� ��� ���
				eeprom_write_word(&stored_data[1][1],FanTemp);
				eeprom_write_word(&stored_data[2][1],FanSpeed);
			}
			else{
				eeprom_write_word(&stored_data[0][1],CopperTemp);
				
			};
			
		break;
		};
	
	
		case 8://8-save2 
		{
			if (CurrentDev==1) { //�������� �������� ��� ���
				eeprom_write_word(&stored_data[1][2],FanTemp);
				eeprom_write_word(&stored_data[2][2],FanSpeed);
			}
			else{
				eeprom_write_word(&stored_data[0][2],CopperTemp);
				
			};
			
		break;
		};


		case 9://9-ch.dev 
		{
			if (CurrentDev==1) CurrentDev=0;
			else CurrentDev=1;
			
		break;
		};


		case 10://10-power 
		{
			if (PowerState==1) {
				FanPowerState=0;
				PowerState=0;

				}
			else {

				PowerState=1;
				CurrentDev=0;
				
				
				};
		break;
		

		};


		case 11://11-fan on\off
		{
			if (FanPowerState>0) 
			{
					FanPowerState=0;
					CurrentDev=0;
					
			}
			else 
			{
				FanPowerState=2;
				CurrentDev=1;
				FanPowerTimer=FAN_TIMEOUT;
				
			};
			

		break;
		};

		case 12://12-fan active
		{
			if (FanPowerState==1) FanPowerState=2;

			FanPowerTimer=FAN_TIMEOUT;
			
			CurrentDev=1;
			

		break;
		};


	};

		if (FanPowerTimer>0) FanPowerTimer--;
		else {
			if (FanPowerState!=0) FanPowerState=1;
		};

		if (SourceValueShowTimer>0) SourceValueShowTimer--;
	
	Lifetime++;	
	if (Lifetime>LIFETIME_LIMIT) {
		PowerState=0;
		FanPowerState=0;
		};
		
};

void EEPROM_Refresh(){ //���������� ������� ��������

	EEMEM_TimeRefresh++;

	if (EEMEM_TimeRefresh>5000) { //��� � ������ ���������� ��������
		if (CopperTemp!=eeprom_read_word(&stored_data[0][0])) eeprom_write_word(&stored_data[0][0],CopperTemp);
		if (FanTemp!=eeprom_read_word(&stored_data[1][0])) eeprom_write_word(&stored_data[1][0],FanTemp);
		if (FanSpeed!=eeprom_read_word(&stored_data[2][0])) eeprom_write_word(&stored_data[2][0],FanSpeed);
		EEMEM_TimeRefresh=0;
	};


};

void DisplaySet(){ //������������ ������ ��� ��������� �� �������


	

	if (SourceValueShowTimer>0) 
		{
		if (CurrentDev==1) display=FanTemp;
		else display=CopperTemp;
		}
	else 
		{
		if (CurrentDev==1) display=(((display+CurrentFanTemp  )>>1)/5)*5;
		else display=(((display+CurrentCopperTemp)>>1)/5)*5;
		};



//	temp=display;




	if (PowerState==0) 
	{
		CurrentDisplayData[0]=12;
		CurrentDisplayData[1]=12;
		CurrentDisplayData[2]=12;
	}
	else
	{
		CurrentDisplayData[0]=display/100;
		CurrentDisplayData[1]=(display-CurrentDisplayData[0]*100)/10;
		CurrentDisplayData[2]=display-CurrentDisplayData[0]*100-CurrentDisplayData[1]*10;

	};





};


void Control_Refresh(){//���������� ������


	if (PowerState==0) //������� ���������
	{
		PWM_FAN(0); 
		OCR1B=0;
		OCR1A=0;
		CurrentCopperPWM=0;


	}

	else {
		
		if ((FanTemp>CurrentFanTemp)&&(FanPowerState>0)) PWM_FAN(1); //�������� ����������� ���� �������� �������
		else PWM_FAN(0); 
		
		switch (FanPowerState){ //�������� ����� ���� �� ������ ���������� �� ������
			case 0:
			{
				OCR1B=0;
				break;
			};
			case 1:
			{
				OCR1B=0x1B0;
				break;
			};
			case 2:
			{
		 		OCR1B=(FanSpeed*80)+0x1B0;
				break;
			};
		};
		

		

		//	CurrentCopperPWM=CurrentCopperPWM+Ki*CopperTemp-(Ki+Kd+Kp)*CurrentCopperTemp+(2*Kd+Kp)*LastCurrentCopperTemp-Kd*TLastCurrentCopperTemp;
		//	TLastCurrentCopperTemp=LastCurrentCopperTemp;
		//	LastCurrentCopperTemp=CurrentCopperTemp;
		
		
		
	//		if (CurrentCopperPWM>1023) CurrentCopperPWM=1023;
	//		if (CurrentCopperPWM<0) CurrentCopperPWM=0;
		

		
		if (CopperTemp>CurrentCopperTemp-3) {
			if ((CopperTemp-CurrentCopperTemp)>30) OCR1A=1023;
			else OCR1A=CopperTemp<<1;
			}
		else {
			if ((CurrentCopperTemp-CopperTemp)>10) OCR1A=0;
			else OCR1A=CopperTemp;
		};
	};


};

void EncCheck(){
	char temp;
	temp=EncoderScan();
	if (temp>0) Command=temp;

};

void KeyCheck()
{

	if (POWER_SW==0) 
	{
		TimeKeyDown[4]++; //������ ������ �������+
		if (TimeKeyDown[4]==(TIME_LIMIT)) Command=10; //������� �������
		
	}
	else 
	{
		
		if ((TimeKeyDown[4]>10)&(TimeKeyDown[4]<(TIME_LIMIT))) Command=9; //������� �������
		TimeKeyDown[4]=0;
	};


	if (FAN_SW==0) 
	{
		TimeKeyDown[5]++; //������ ������ �������+
		if (TimeKeyDown[5]==(TIME_LIMIT)) Command=11; //������� �������
	}
	else 
	{
		if ((TimeKeyDown[5]>10)&(TimeKeyDown[5]<(TIME_LIMIT))) Command=12; //������� �������
		TimeKeyDown[5]=0; //������ ������ ��������������
		
	};
};


int main(void){


	int counter=0;
	
	Init();
	
	ADCSRA|=1<<ADSC;
	
	sei();
	
	while (1){

	Command_Check(Command);
	Command=0;
	
	KeyCheck();

	DisplaySet();
	
	counter++;

	if (counter>4) {
		TCCR1A=0b00000010;
		_delay_ms(5);
		ADCSRA|=1<<ADSC;
		_delay_ms(5);
		Control_Refresh();
		counter=0;
	};
	
	EEPROM_Refresh();
	_delay_ms(10);
	
	};
	
};








ISR(ADC_vect)// ��������� ���
{

	if (CurrentADC==0) 
	{
		CurrentADC=1;
		ADMUX =ADMUX|1;
		CurrentCopperTemp=ADC>>1;
	}
		
	else 
	{
		CurrentADC=0;
		ADMUX =ADMUX&0b11111110;
		CurrentFanTemp=ADC>>1;

	};

	TCCR1A=0b10100010;

};




ISR(TIMER0_OVF_vect) //������ �������
{
	
	cli();

	LED_IN(0xFF);

	if (CurrentChannel<3) CurrentChannel++;
	else CurrentChannel=0;


    LED_OUT(CurrentChannel);
	


	if (CurrentChannel==3)
	{
		if ((CurrentDev==1)&&(PowerState==1)) {
			switch (FanPowerState)	{
				case 0:
				{
					LED_IN(0b1000000>>(FanSpeed-1));
					break;
				}
				case 1:
				{
					LED_IN(((0b1000000>>(FanSpeed-1))|0b1000000));
					break;
				}
				case 2:
				{
					LED_IN(0b1111111<<(7-FanSpeed));
					break;
				}
			};	
		}
		else
		{
		LED_IN(0);
		};

			
	}
	else
	{
	LED_IN(LedDisplaySegments[CurrentDisplayData[CurrentChannel]]);

	};


//�������� ������
	
	EncCheck();

	switch (CurrentChannel)
	{
		case SET_SW1_OUT:
			if (SET_SW1) 
			{
				TimeKeyDown[SET_SW1_OUT]++; //������ ������ �������+
				if (TimeKeyDown[SET_SW1_OUT]==TIME_LIMIT<<4) Command=7; //������� �������
			}
			else 
			{
				
				if ((TimeKeyDown[SET_SW1_OUT]>1)&(TimeKeyDown[SET_SW1_OUT]<(TIME_LIMIT<<4))) Command=5;
				TimeKeyDown[SET_SW1_OUT]=0;
			
			};

		break;

		case SET_SW2_OUT:

			if (SET_SW2) 
			{
				TimeKeyDown[SET_SW2_OUT]++; //������ ������ �������+
				if (TimeKeyDown[SET_SW2_OUT]==TIME_LIMIT<<4) Command=8; //������� �������
			}
			else 
			{
				
				if ((TimeKeyDown[SET_SW2_OUT]>1)&(TimeKeyDown[SET_SW2_OUT]<(TIME_LIMIT<<4))) Command=6; //������� �������
				TimeKeyDown[SET_SW2_OUT]=0;

			};

		break;
		
		case FAN_UP_SW_OUT:
			
			if (FAN_UP_SW) 
			{
				TimeKeyDown[FAN_UP_SW_OUT]++; //������ ������ �������+
			}
			else 
			{
				if (TimeKeyDown[FAN_UP_SW_OUT]>1) Command=3; //������� �������
				TimeKeyDown[FAN_UP_SW_OUT]=0; 
			};

		break;
		
		case FAN_DOWN_SW_OUT:
			
			if (FAN_DOWN_SW) 
			{
				TimeKeyDown[FAN_DOWN_SW_OUT]++; //������ ������ �������+
			}
			else 
			{
				
				if (TimeKeyDown[FAN_DOWN_SW_OUT]>1) Command=4; //������� �������
				TimeKeyDown[FAN_DOWN_SW_OUT]=0; 
						
			};

		break;
		
	};
	
	sei();
};

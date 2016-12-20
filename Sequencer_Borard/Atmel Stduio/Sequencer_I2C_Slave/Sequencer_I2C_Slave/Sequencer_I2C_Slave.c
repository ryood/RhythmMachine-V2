/*
 * Sequencer_I2C_Slave.c
 *
 * Created: 2015/08/26 17:08:41
 *  Author: gizmo
 *
 * �N���b�N�F����8MHz
 * �f�o�C�X�FATmega88V
 * Fuse bit�FhFuse:DFh lFuse:E2h eFuse:01h
 *
 * PC4:SDA
 * PC5:SCK
 *
 * PortD[PD0..PD4]: SWx5
 * PortB[PB6..PB7]: SWx2
 * PortD[PD5]     : SWx1
 *
 * PortD[PD6..PD7]: LEDx2
 * PortD[PB0]     : SWx1
 *
 * PORTB[PB1]     : Rotary Encoder SW
 * PORTB[PB2]     : Rotary Encoder A
 * PORTB[PB3]     : Rotary Encoder B
 * PORTC[PC3]     : LED
 *
 * PORTB[PB4]     : 74HC595 SER
 * PORTB[PB5]     : 74HC595 RCK
 * PORTC[PC0]     : 74HC595 SCK
 *
 * AtmelStudio 6.2
 *
 * 2015.11.15 �g���b�N����8�ɕύX
 * 2015.10.31 POT1/POT2��4bit���x��
 * 2015.10.29 I2C���M�O�Ƀp�����[�^���X�g�A
 * 2015.10.29 �g���b�N�̊Ǘ�
 * 2015.10.27 ADC�̓ǂݎ��l�𕽋ω�
 * 2015.10.27 ���M�f�[�^�̕ύX
 * 2015.09.28 �Đ����̃m�[�g����M
 * 2015.09.08 POT�̏�����ǉ�
 * 2015.09.08 Rotary Encoder�̏�����ǉ�
 * 2015.09.01 �V�[�P���X��2�o�C�g���M
 * 2015.09.01 TWI�G���[���ɃX�e�[�^�X�R�[�h��LED�ɕ\��
 * 2015.09.01 �f�o�b�O�p��PC3��LED��ڑ�
 * 2015.09.01 �V�[�P���X��z��ɕύX
 * 2015.08.30 Pin Change Interrupt�ƃ`���^�����O�΍�
 * 2015.08.30 I2C�����荞�ݏ���
 *
 */ 

#define F_CPU 8000000UL  // 8MHz

#include 	<avr/io.h>
#include	<avr/interrupt.h>
#include	<util/delay.h>

// TWI �X���[�u�E�A�h���X
#define TWI_SLAVE_ADDRESS	0xFE

// TWI �X�e�[�^�X�R�[�h
// Rx
#define	TWI_SLA_W_ACK		0x60
#define	TWI_RX_DATA_ACK		0x80
#define	TWI_RX_STOP			0xA0
// Tx
#define	TWI_SLA_R_ACK		0xA8
#define	TWI_TX_DATA_ACK		0xB8
#define	TWI_TX_DATA_NACK	0xC0

// Shift Register
#define SHIFT_PORT PORTB
#define SHIFT_DATA PB4
#define SHIFT_RCK PB5

#define SHIFT_PORT_SCK PORTC
#define SHIFT_SCK PC0

// Track Data
#define TRACK_N	8

// ADC
#define ADC_BUFFER_LEN	8

// ���ϐ�
//
// �p�����[�^�X�g�A�p�o�b�t�@
volatile struct sequence_parameter {
	uint8_t update;
	uint8_t track;
	uint8_t play;
	uint8_t sequence1;
	uint8_t sequence2;
	uint8_t pot1;
	uint8_t pot2;	
} sequence_parameter_buffer;

// �ύX�̗L���t���O
volatile uint8_t isDataDirty;

// �V�[�P���X�E�X�C�b�`
volatile uint8_t sequence_data[TRACK_N][2];			// �V�[�P���X�E�X�C�b�`�̃g�O�����
volatile uint8_t sequence_n;						// �V�[�P���X�̕\�� 
volatile uint8_t sequence_rd;						// �V�[�P���X�E�X�C�b�`�̓ǂݎ��l
volatile uint8_t sequence_n_rd;

volatile uint8_t prev_sequence_data[TRACK_N][2];	// �ύX�̗L������p�ɒl��ۑ�
volatile uint8_t prev_sequence_n;

// Potentiometer
volatile uint8_t pot_data[2];
volatile uint8_t pot_n;

volatile uint8_t prev_pot_data[2];
volatile uint8_t prev_pot_n;

volatile uint8_t adc_buffer[2][ADC_BUFFER_LEN];
volatile uint8_t adc_buffer_n[2];

// Rotary Encoder
volatile uint8_t re_data;		// track�ԍ�
volatile uint8_t re_sw;
volatile uint8_t re_sw_rd;

volatile uint8_t prev_re_data;
volatile uint8_t prev_re_sw;

// �Đ�����note�i���o�[
volatile uint8_t playing_note_n;

// TWI
volatile uint8_t twi_data_n;

void shift_out(uint8_t data);

//------------------------------------------------//
// �p�����[�^�̃X�g�A
//
//------------------------------------------------//
void store_pamaeter()
{
	sequence_parameter_buffer.update = isDataDirty;
	sequence_parameter_buffer.track = re_data;
	sequence_parameter_buffer.play = re_sw ? 0 : 1;		// 1:�Đ� 0:��~
	sequence_parameter_buffer.sequence1 = sequence_data[re_data][0]; 
	sequence_parameter_buffer.sequence2 = sequence_data[re_data][1];
	sequence_parameter_buffer.pot1 = pot_data[0];
	sequence_parameter_buffer.pot2 = pot_data[1];
}

//------------------------------------------------//
// TWI
//
//------------------------------------------------//
void twi_error()
{
	uint8_t twi_status = TWSR & 0xF8;
	
	shift_out(twi_status);
	
	while(1) {
		PORTD = 0b10000000;
		_delay_ms(100);
		PORTD = 0b01000000;
		_delay_ms(100);
	}
}

void twi_init()
{
	// 8MHz clk, bit rate 100kHz
	TWBR = 2;
	TWSR = 0x02;
	
	//slave address
	TWAR = TWI_SLAVE_ADDRESS;
	
	// ���荞�݂�����
	// Enable TWI port, ACK, IRQ and clear interrupt flag
	TWCR = ((1<<TWEN) | (1<<TWEA) | (1<<TWIE) | (1<<TWINT));
}

ISR (TWI_vect)
{
	// ���荞�݂��Ƃ�LED��_�Łi�f�o�b�O�p�j
	//PORTC ^= (1 << PC3);
	
	switch (TWSR & 0xF8) {
	case TWI_SLA_R_ACK:
		twi_data_n = 0;
		// ADC�̓ǂݎ��l�̕ύX�̃`�F�b�N
		for (int i = 0; i < 2; i++) {
			if (pot_data[i] != prev_pot_data[i]) {
				prev_pot_data[i] = pot_data[i];
				isDataDirty |= (1 << (i + 5));
			}
		}
		// �p�����[�^�̃X�g�A
		store_pamaeter();
		// isDataDirty���N���A
		isDataDirty = 0;
		// through down to TWI_TX_DATA_ACK
		
	// Slave TX
	//
	case TWI_TX_DATA_ACK:
		switch (twi_data_n) {
		case 0:
			// No.1 �ύX�̗L��
			TWDR = sequence_parameter_buffer.update;
			break;
		case 1:
			// No.2 �g���b�N�ԍ�
			TWDR = sequence_parameter_buffer.track;
			break;
		case 2:
			// No.3 �Đ��t���O
			TWDR = sequence_parameter_buffer.play;
			break;
		case 3:
			// No.4 �V�[�P���X�̃g�O�����(�\:1..8)
			TWDR = sequence_parameter_buffer.sequence1;
			break;
		case 4:
			// No.5 �V�[�P���X�̃g�O�����(��:9..16)
			TWDR = sequence_parameter_buffer.sequence2;
			break;
		case 5:
			// No.6 POT1��ADC�̓ǂݎ��l
			TWDR = sequence_parameter_buffer.pot1;
			break;
		case 6:
			// No.7 POT2��ADC�̓ǂݎ��l
			TWDR = sequence_parameter_buffer.pot2;
			break;
		default:
			twi_error();
		}
		twi_data_n++;
		break;
	case TWI_TX_DATA_NACK:
		break;
		
	// Slave RX
	//
	case TWI_SLA_W_ACK:
		break;
	case TWI_RX_DATA_ACK:
		playing_note_n = TWDR;
		break;
	case TWI_RX_STOP:
		break;
	default:
		twi_error();
	}
	
	TWCR |= (1 << TWINT);	// Clear TWI interrupt flag
}

//------------------------------------------------//
// Shift Register (74HC595)
//
//------------------------------------------------//

//�V�t�g���W�X�^�N���b�N������M
void _shift_sck()
{	
	SHIFT_PORT_SCK |= (1<<SHIFT_SCK);
	SHIFT_PORT_SCK &= ~(1<<SHIFT_SCK);
}

//���b�`�N���b�N������M
void _shift_rck()
{	
	SHIFT_PORT &= ~(1<<SHIFT_RCK);
	SHIFT_PORT |= (1<<SHIFT_RCK);
}

//�V���A���f�[�^��SER�ɏo��
void _shift_data(uint8_t bit)
{
	if (bit) {
		SHIFT_PORT |= (1<<SHIFT_DATA);
	} else {
		SHIFT_PORT &= ~(1<<SHIFT_DATA);
	}
}

void shift_out(uint8_t data)
{
	int8_t i;
	for (i=7; i>=0; i--) {   //��ʃr�b�g����W���M
		_shift_data((data>>i)&1);
		_shift_sck();
	}
	_shift_rck();   //���b�`���X�V
}

//------------------------------------------------//
// Switch�̏���
//
//------------------------------------------------//

void init_switches()
{
	// Pin Change Interrupt�̗L����
	PCICR = (1 << PCIE0) | (1 << PCIE2);
	PCMSK0 = 0b11000011;	// PORTB
	PCMSK2 = 0b00111111;	// PORTD
	
	// TIMER0 �I�[�o�[�t���[���荞�݂̗L����
	TCCR0B = 0x00;	// Timer0��~
	TIMSK0 = (1 << TOIE0);
}

// �V�[�P���X�E�X�C�b�`�̉���������Ԃ�ǂݎ��
uint8_t read_sequence_switches()
{
	uint8_t data;
	
	data  = (~PIND & 0b00011111);
	data |= ((~PINB & 0b11000000) >> 1);
	data |= ((~PIND & 0b00100000) << 2);
	
	return data;
}

ISR (TIMER0_OVF_vect)
{
	uint8_t tmp;
	
	// Timer0���~
	TCCR0B = 0x00;
	
	// �V�[�P���X�\���؂�ւ��X�C�b�`���g�O������œǂݎ��
	// PINx���W�X�^�̒l�͂�������ϐ��ɑ�����Ȃ��Ɣ�r�����܂������Ȃ�
	tmp = (~PINB & (1 << PB0));
	if (sequence_n_rd == tmp) {
		prev_sequence_n = sequence_n;
		sequence_n ^= sequence_n_rd;
		
		if (sequence_n != prev_sequence_n) {
			// �g�O����Ԃ�LED�ɕ\��
			if (sequence_n) {
				PORTD &= ~(1 << PD6);
				PORTD |= (1 << PD7);
			} else {
				PORTD &= ~(1 << PD7);
				PORTD |= (1 << PD6);
			}
		}
	}
	
	// �V�[�P���X�E�X�C�b�`��̓ǂݎ��
	tmp = read_sequence_switches();
	if (sequence_rd == tmp) {
		prev_sequence_data[re_data][sequence_n] = sequence_data[re_data][sequence_n];
		sequence_data[re_data][sequence_n] ^= sequence_rd;
		
		if (sequence_data[re_data][sequence_n] != prev_sequence_data[re_data][sequence_n]) {
			isDataDirty |= 1 << (sequence_n + 3);
		}
	}
	
	// Rotary Encoder�̃X�C�b�`�̓ǂݎ��
	if (re_sw_rd == (~PINB & (1 << PB1))) {
		prev_re_sw = re_sw;
		re_sw ^= re_sw_rd;
		
		if (re_sw != prev_re_sw) {
			isDataDirty |= (1 << 2);
		}
		
		// �g�O����Ԃ�LED�ɕ\��
		if (re_sw) {
			PORTC |= (1 << PC3);
		} else {
			PORTC &= ~(1 << PC3);
		}
	}
		
	// Pin Change Interrupt�̗L����
	PCICR = (1 << PCIE0) | (1 << PCIE2);
}

void pin_change_interrupt_handler()
{
	// Pin Change Interrupt�𖳌���
	PCICR = 0x00;
	
	// ���荞�݂��Ƃ�LED��_�Łi�f�o�b�O�p�j
	//PORTC ^= (1 << PC3);
		
	sequence_rd = read_sequence_switches();
	sequence_n_rd = (~PINB & (1 << PB0));	// �V�[�P���X�ؑփX�C�b�`
	re_sw_rd = (~PINB & (1 << PB1));		// Rotary Encoder�̃X�C�b�`
	
	// Timer0���N��
	TCCR0B = 0x05;	// �v���X�P�[���|:1024, 1/(8MHz/1024)=128us
	//TCNT0 = 100;	// 128us*(256-100)=19.968ms
	TCNT0 = 0;	    // 128us*(256-  0)=32.768ms
}

ISR (PCINT0_vect)
{
	pin_change_interrupt_handler();
}

ISR (PCINT2_vect)
{
	pin_change_interrupt_handler();
}

//------------------------------------------------//
// Potentiometer
//
//------------------------------------------------//
ISR(ADC_vect)
{
	int16_t data_sum;
	
	//adc_buffer[pot_n][adc_buffer_n[pot_n]] = ADCH;			// 8bit Resolution
	adc_buffer[pot_n][adc_buffer_n[pot_n]] = ADCH >> 4;	// 4bit Resolution
	
	adc_buffer_n[pot_n]++;
	if (adc_buffer_n[pot_n] == ADC_BUFFER_LEN) {
		adc_buffer_n[pot_n] = 0;
		
		// ADC�̓ǂݎ��l�𕽋ω�
		data_sum = 0;
		for (int i = 0; i < ADC_BUFFER_LEN; i++) {
			data_sum += adc_buffer[pot_n][i];
		}
		pot_data[pot_n] = data_sum / ADC_BUFFER_LEN;
	}
	
	// ����ADC���N��
	switch (pot_n) {
	case 0:
		pot_n = 1;
		// ���t�@�����X�d��: AVCC, �ϊ����ʂ͍��l��, ADC2�V���O���G���h����
		ADMUX = (1 << REFS0) | (1 << ADLAR) | (1 << MUX1);
		ADCSRA |= (1 << ADSC);		// Start Conversion
		break;
	case 1:
		pot_n = 0;
		// ���t�@�����X�d��: AVCC, �ϊ����ʂ͍��l��, ADC1�V���O���G���h����
		ADMUX = (1 << REFS0) | (1 << ADLAR) | (1 << MUX0);
		ADCSRA |= (1 << ADSC);		// Start Conversion
		break;
	}
}

//------------------------------------------------//
// Rotary Encoder
//
//------------------------------------------------//
// �߂�l: ���[�^���[�G���R�[�_�[�̉�]����
//         0:�ω��Ȃ� 1:���v��� -1:�����v���
//
int8_t read_re(void)
{
	static uint8_t index;
	int8_t ret_val = 0;
	uint8_t rd;
	
	rd = ((PINB & 0b00001100) >> 2);
	
	_delay_us(500);
	
	if (rd == ((PINB & 0b00001100) >> 2)) {
		//PORTC ^= (1 << PC3);	// (�f�o�b�O�p)
		
		index = (index << 2) | rd;
		index &= 0b1111;
		
		switch (index) {
			// ���v���
			case 0b0001:	// 00 -> 01
			case 0b1110:	// 11 -> 10
			ret_val = 1;
			break;
			// �����v���
			case 0b0010:	// 00 -> 10
			case 0b1101:	// 11 -> 01
			ret_val = -1;
			break;
		}
	}
	
	return ret_val;
}

//------------------------------------------------//
// Main routine
//
//------------------------------------------------//

int main()
{
	DDRB = 0x00;
	DDRC = 0x00;
	DDRD = 0x00;

	// Switch input / pull up
	PORTD |= 0b00111111;
	PORTB |= 0b11000011;
	
	// Rotary Encoder input / pull up
	PORTB |= 0b00001100;
	
	// Shift Register: SER, SCK, RCK output
	DDRB |= (1 << SHIFT_DATA) | (1 << SHIFT_RCK);
	DDRC |= (1 << SHIFT_SCK);
	
	// LED
	DDRD |= (1 << PD7) | (1 << PD6);
	DDRC |= (1 << PC3);
	
	// LED Check
	PORTD |= (1 << PD7) | (1 << PD6);
	PORTC |= (1 << PC3);
	for (int i = 0; i <= 8; i++) {
		shift_out(0xFF >> i);
		_delay_ms(100);
	}
	PORTD &= ~(1 << PD6);
	_delay_ms(100);
	PORTD &= ~(1 << PD7);
	_delay_ms(100);
	PORTC &= ~(1 << PC3);
		
	init_switches();
	PORTD |= (1 << PD6);		// �g�O���\���̏����l
	
	twi_init();
	
	// Potentiometer
	//
	// Enable the ADC and its interrupt feature
	// and set the ACD clock pre-scalar to clk/128
	ADCSRA = (1 << ADEN) | (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
	
	// ���t�@�����X�d��: AVCC, �ϊ����ʂ͍��l��, ADC1�V���O���G���h����
	ADMUX = (1 << REFS0) | (1 << ADLAR) | (1 << MUX0);
	pot_n = 0;
		
	sei();
	
	ADCSRA |= (1 << ADSC);		// Start Conversion
	
	for(;;) {
		// Rotary Encoder�̓ǂݎ��
		//
		prev_re_data = re_data;
		re_data += read_re();
		
		// TRACK���͈̔͂ɐ؂�l��
		if (re_data >= 128)
			re_data = 0;
		if (re_data >= TRACK_N)
			re_data = TRACK_N - 1;
		
		if (re_data != prev_re_data) {
			isDataDirty |= (1 << 1);
		}
		
		// �V�[�P���XLED�̕\��
		//
		uint16_t led_pos = 0; 
		if (re_sw == 0) {
			// �Đ���
			// �V�[�P���X�̕\���ɍ��킹��Note��LED�\���ʒu�Đݒ�
			led_pos = (1 << playing_note_n);
			led_pos = (sequence_n ? (led_pos >> 8) : led_pos);
			led_pos &= 0xff;
		}
		
		// �V�[�P���X�̃g�O����Ԃ�Note�̕\���ʒu��OR����LED��_��
		shift_out(sequence_data[re_data][sequence_n] | led_pos);
	}
}

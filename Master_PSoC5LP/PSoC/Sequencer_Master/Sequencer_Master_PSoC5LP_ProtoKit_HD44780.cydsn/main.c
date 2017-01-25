/* ========================================
 *
 * RhythmMachine
 *   PSoC 5LP Prototyping Kit
 *
 * 2017.01.25 V2
 * 2015.11.12 新規作成
 *
 * ========================================
*/
#include <project.h>
#include <stdio.h>
#include <stdlib.h>

#include "utility.h"
#include "dds.h"
#include "WaveTableFP32.h"
#include "ModTableFP32.h"

#define TITLE_STR   ("Rhythm Machine 2")
#define VERSION_STR ("2017.01.25")

// Sequencer
//
#define SEQUENCER_I2C_SLAVE_ADDRESS   (0x7f)
#define SEQUENCER_I2C_WR_BUFFER_SIZE  (1u)

#define SEQUENCER_TRANSFER_CMPLT    (0x00u)
#define SEQUENCER_RX_TRANSFER_ERROR (0xFEu)
#define SEQUENCER_TX_TRANSFER_ERROR (0xFFu)

// Rotary Encoder
#define RE_DECAY    (0x00)
#define RE_TONE     (0x01)
#define RE_LEVEL    (0x02)

// Tact Switch
#define TACT_SW1    (0x00)
#define TACT_SW2    (0x01)

// Error
//
#define ERR_SEQUENCER_READ      (0x01)
#define ERR_SEQUENCER_WRITE     (0x02)
#define ERR_RE_OUT_OF_BOUNDS    (0x03)

/**************************************************
 データ型
***************************************************
waveLookupTable   : fp32 Q16 : -1.0 .. +1.0
decayLookupTable  : fp32 Q16 : -1.0 .. +1.0

wavePhaseRegister : 32bit
waveTunigWord     : 32bit
decayPhaseRegister: 32bit
decayTuningWord   : 32bit

waveFrequency     : double
levelAmount       : 8bit
toneAmount        : 8bit    // 未実装
decayAmount       : 8bit

bpmAmount         : 8bit
volumeAmount      : 8bit

***************************************************/

//=================================================
// マクロ関数
// 
//=================================================
#define RGB_LED_ON_RED      LED_RGB_Write(0x04);
#define RGB_LED_ON_BLUE     LED_RGB_Write(0x02);
#define RGB_LED_ON_GREEN    LED_RGB_Write(0x01);
#define RGB_LED_OFF         LED_RGB_Write(0x00);

//=================================================
// 大域変数
// 
//=================================================
//-------------------------------------------------
// Sequencer                
//
struct sequencer_parameter sequencerRdBuffer;
uint8 sequencerWrBuffer[SEQUENCER_I2C_WR_BUFFER_SIZE] = {0};

uint8 sequencerRdStatus;
uint8 sequencerWrStatus;

struct track tracks[TRACK_N];

//-------------------------------------------------
// Rotary Encoder                
//
uint8 isREDirty;

//-------------------------------------------------
// Ext Tact Switches
//
uint8 tactSwitch;

//=================================================
// LCD
// 
//=================================================                
//-------------------------------------------------
// LCD printf
// Parameter: line:   表示する桁 (0..1)
//            format: 書式
//            ...:    可変引数リスト
void LCD_printf(int line, const char *format, ...)
{
    va_list ap;
    uint8 buf[256];
    
    va_start(ap, format);
    xvsnprintf(buf, 256, format, ap);
    va_end(ap);

    //LCD_Char_ClearDisplay();
    LCD_Char_Position(line, 0);
    LCD_Char_PrintString((char8 *)buf);
}

//-------------------------------------------------
// エラー処理
// parameter: code: エラーコード
//            ext:  付帯数値
void error(uint32 code, uint32 ext)
{
    char8 error_str[][17] = {
        "",
        "Sequencer Rd Err",   // 0x01
        "Sequencer Wt Err",   // 0x02
        "RE Out of Bounds",   // 0x03
    };
    
    LCD_Char_ClearDisplay();
    LCD_printf(0, "%s", error_str[code]);
    LCD_printf(1, "%08x", ext);
    
    // loop for ever
    for (;;);
}

//-------------------------------------------------
// シーケンサーパラメータ表示
//
//-------------------------------------------------
// シーケンス表示用文字列生成
// parameter: buffer: 文字列格納バッファ
//            sequence1: シーケンス1
//            sequence2: シーケンス2
void sequenceString(char *buffer, uint8 sequence1, uint8 sequence2)
{
    const char charOnOff[] = { '.', 'o' };
    int i;
    
    for (i = 0; i < 8; i++) {
        buffer[i] = charOnOff[(sequence1 & (1 << i)) >> i];
    }
    for (i = 0; i < 8; i++) {
        buffer[i + 8] = charOnOff[(sequence2 & (1 << i)) >> i];
    }
    if (sequencerRdBuffer.play) {
        buffer[sequencerWrBuffer[0]] = '^';
    }        
}

//-------------------------------------------------
// シーケンス表示用カスタム文字列生成
// parameter: buffer: 文字列格納バッファ
//            sequence1: シーケンス1
//            sequence2: シーケンス2
void sequenceCustomFontString(char *buffer, uint8 sequence1, uint8 sequence2)
{
    const char charOnOff[]        = { LCD_Char_CUSTOM_1, LCD_Char_CUSTOM_2 };
    const char charPlayingOnOff[] = { LCD_Char_CUSTOM_3, LCD_Char_CUSTOM_4 };
    int i;
    
    for (i = 0; i < 8; i++) {
        if (sequencerWrBuffer[0] == i) {
            buffer[i] = charPlayingOnOff[(sequence1 & (1 << i)) >> i];
        } else {
            buffer[i] = charOnOff[(sequence1 & (1 << i)) >> i];
        }
    }
    for (i = 0; i < 8; i++) {
        if (sequencerWrBuffer[0] == i + 8) {
            buffer[i + 8] = charPlayingOnOff[(sequence2 & (1 << i)) >> i];
        } else {
            buffer[i + 8] = charOnOff[(sequence2 & (1 << i)) >> i];
        }
    }
}
void displaySequencerParameter()
{
    const char *strTracks[] = { 
        "KIK", "SNR", "HHC", "HHO", "LOT", "MIT", "HIT", "RIM"
    };
    char lineBuffer[17];
    
    LCD_Char_Position(0, 0);
    LCD_printf(0, "%03d %s %2u%3d %2u",
        (sequencerRdBuffer.pot2 << 4) | sequencerRdBuffer.pot1,
        strTracks[sequencerRdBuffer.track],
        tracks[sequencerRdBuffer.track].levelAmount >> 2,
        tracks[sequencerRdBuffer.track].toneAmount >> 2,
        tracks[sequencerRdBuffer.track].decayAmount >> 2
    );
    
    //sequenceString(lineBuffer, sequencerRdBuffer.sequence1, sequencerRdBuffer.sequence2);
    sequenceCustomFontString(lineBuffer, sequencerRdBuffer.sequence1, sequencerRdBuffer.sequence2);
    LCD_Char_Position(1, 0);
    LCD_Char_PrintString(lineBuffer);
}

//=================================================
// Sequencer
//
//=================================================
//-------------------------------------------------
// Sequencerから受信
// return: Error code
//
uint32 readSequencerBoard(void)
{
    uint32 status = SEQUENCER_RX_TRANSFER_ERROR;
    
    I2CM_Sequencer_MasterReadBuf(
        SEQUENCER_I2C_SLAVE_ADDRESS, 
        (uint8 *)&sequencerRdBuffer, 
        sizeof(sequencerRdBuffer), 
        I2CM_Sequencer_MODE_COMPLETE_XFER
    );
    
    while (0u == (I2CM_Sequencer_MasterStatus() & I2CM_Sequencer_MSTAT_RD_CMPLT))
    {
        /* Waits until master completes read transfer */
    }
    
    /* Displays transfer status */
    if (0u == (I2CM_Sequencer_MSTAT_ERR_XFER & I2CM_Sequencer_MasterStatus()))
    {
        RGB_LED_ON_GREEN;

        /* Check if all bytes was written */
        if (I2CM_Sequencer_MasterGetReadBufSize() == sizeof(sequencerRdBuffer))
        {
            status = SEQUENCER_TRANSFER_CMPLT;
        }
    }
    else
    {
        RGB_LED_ON_RED;
    }
    
    sequencerRdStatus = I2CM_Sequencer_MasterStatus();
    (void) I2CM_Sequencer_MasterClearStatus();
    
    return status;
}

//-------------------------------------------------
// Sequencerに送信
// return: Error code
//
uint32 writeSequencerBoard(void)
{
    uint32 status = SEQUENCER_TX_TRANSFER_ERROR;
    
    I2CM_Sequencer_MasterWriteBuf(SEQUENCER_I2C_SLAVE_ADDRESS,
        sequencerWrBuffer,
        SEQUENCER_I2C_WR_BUFFER_SIZE,
        I2CM_Sequencer_MODE_COMPLETE_XFER
    );
    
    while (0u == (I2CM_Sequencer_MasterStatus() & I2CM_Sequencer_MSTAT_WR_CMPLT))
    {
        /* Waits until master completes read transfer */
    }
    
    /* Displays transfer status */
    if (0u == (I2CM_Sequencer_MSTAT_ERR_XFER & I2CM_Sequencer_MasterStatus()))
    {
        RGB_LED_ON_GREEN;

        /* Check if all bytes was written */
        if (I2CM_Sequencer_MasterGetWriteBufSize() == SEQUENCER_I2C_WR_BUFFER_SIZE)
        {
            status = SEQUENCER_TRANSFER_CMPLT;
        }
    }
    else
    {
        RGB_LED_ON_RED;
    }

    sequencerWrStatus = I2CM_Sequencer_MasterStatus();
    (void) I2CM_Sequencer_MasterClearStatus();

    return status;
}

//=================================================
// Rotary Encoder
//
//=================================================
//-------------------------------------------------
// ロータリーエンコーダの読み取り
// return: ロータリーエンコーダーの回転方向
//         0:変化なし 1:時計回り -1:反時計回り
//
int readRE_1(int RE_n)
{
    static uint8_t index[3];
    uint8_t rd = 0;
    int retval = 0;
    
    switch (RE_n) {
    case 0:
        rd = Pin_RE1_Read();
        break;
    case 1:
        rd = Pin_RE2_Read();
        break;
    case 2:
        rd = Pin_RE3_Read();
        break;
    default:
        error(ERR_RE_OUT_OF_BOUNDS, RE_n);
    }

    index[RE_n] = (index[RE_n] << 2) | rd;
	index[RE_n] &= 0b1111;

	switch (index[RE_n]) {
	// 時計回り
	case 0b0001:	// 00 -> 01
	case 0b1110:	// 11 -> 10
	    retval = 1;
	    break;
	// 反時計回り
	case 0b0010:	// 00 -> 10
	case 0b1101:	// 11 -> 01
	    retval = -1;
	    break;
    }
    return retval;
}
//-------------------------------------------------
// Decay値を取得
// return: decay値
//
uint8 readDecay()
{   
    int rv;
    static int16 amt;
    
    rv = readRE_1(RE_DECAY);
    if (rv != 0) {
        amt += rv;
        amt = tracks[sequencerRdBuffer.track].decayAmount;
        amt += rv << 1;
        if (amt > 0 && amt <= UINT8_MAX) {
            isREDirty |= (1 << RE_DECAY);
            tracks[sequencerRdBuffer.track].decayAmount = amt;
            setModDDSParameter(&tracks[sequencerRdBuffer.track]);
        }
        //LCD_printf(1, "%d %d  ", rv, amt); 
    }
    return amt;
}

//-------------------------------------------------
// Level値を取得
// return: level値
//
uint8 readLevel()
{
    int rv;
    static int16 amt;
    
    rv = readRE_1(RE_LEVEL);
    if (rv != 0) {
        amt += rv;
        amt = tracks[sequencerRdBuffer.track].levelAmount;
        amt += rv << 1;
        if (amt >= 0 && amt <= UINT8_MAX) { 
            isREDirty |= (1 << RE_LEVEL);
            tracks[sequencerRdBuffer.track].levelAmount = amt;
        }
        //LCD_printf(1, "%d %d  ", rv, amt); 
    }
    return amt;
}

//-------------------------------------------------
// Tone値を取得
// return: tone値
//
uint8 readTone()
{
    int rv;
    static int16 amt;
    
    rv = readRE_1(RE_TONE);
    if (rv != 0) {
        amt += rv;
        amt = tracks[sequencerRdBuffer.track].toneAmount;
        amt += rv << 1;
        if (amt >= INT8_MIN && amt <= INT8_MAX) { 
            isREDirty |= (1 << RE_TONE);
            tracks[sequencerRdBuffer.track].toneAmount = amt;
        }
        //LCD_printf(1, "%d %d  ", rv, amt); 
    }
    return amt;
}


//=================================================
// Tact Switch
//
//=================================================
void readTactSwitch()
{
    tactSwitch = Pin_SW_Read();    
}

//=================================================
// Sampling Timer
//
//=================================================
//-------------------------------------------------
// 割り込み処理ルーチン
//
CY_ISR(Timer_Sampling_interrupt_handler)
{
    fp32 fv, fv8;
    uint8 i8v, i8v_plus;
    
    // デバッグ用
    //Pin_ISR_Check_Write(1u);
    
    /* Read Status register in order to clear the sticky Terminal Count (TC) bit 
	 * in the status register. Note that the function is not called, but rather 
	 * the status is read directly.
	 */
   	Timer_Sampling_STATUS;
    
    if (sequencerRdBuffer.play) {

        fv = generateWave(tracks);
    
        // for 8bit output (0..255)
    	// 128で乗算すると8bit幅を超えるため127で乗算
    	fv8 = fp32_mul(fv + int_to_fp32(1), int_to_fp32(127));
        i8v = fp32_to_int(fv8);
        
        // LED表示用に正側のみを取得
        i8v_plus = (i8v - 128) > 0 ? (i8v - 128) << 1 : 0;
        
        VDAC8_1_SetValue(i8v);
        VDAC8_2_SetValue(i8v_plus);
    }        
    
    // デバッグ用
    //Pin_ISR_Check_Write(0u);
}

//=================================================
// 波形の初期化
// parameter: tracks: トラックデータの配列
//
//================================================= 
void initTracks(struct track *tracks)
{
	// Kick
	tracks[0].waveLookupTable = waveTableSine;
	tracks[0].decayLookupTable = modTableLinerDown01;
	tracks[0].waveFrequency = 50.0f;
	tracks[0].decayAmount = 64;
	tracks[0].levelAmount = 200;
    tracks[0].levelMax = 255;
	tracks[0].toneAmount = 0;
	//memcpy(tracks[0].sequence, kickSequence, SEQUENCE_LEN);

	// Snare
	tracks[1].waveLookupTable = waveTableSine;
	tracks[1].decayLookupTable = modTableLinerDown01;
	tracks[1].waveFrequency = 120.0f;
	tracks[1].decayAmount = 64;
	tracks[1].levelAmount = 200;
    tracks[1].levelMax = 255;
	tracks[1].toneAmount = 0;
	//memcpy(tracks[1].sequence, snareSequence, SEQUENCE_LEN);

	// HiHat Close
	tracks[2].waveLookupTable = waveTableSine;	// unused
	tracks[2].decayLookupTable = modTableRampDown01;
	tracks[2].waveFrequency = 2500.0f;			// unused
	tracks[2].decayAmount = 64;
	tracks[2].levelAmount = 64;
    tracks[2].levelMax = 128;
	tracks[2].toneAmount = 0;
	//memcpy(tracks[2].sequence, hihatCloseSequnce, SEQUENCE_LEN);

	// HiHat Open
	tracks[3].waveLookupTable = waveTableSine;	// unused
	tracks[3].decayLookupTable = modTableSustainBeforeRampDown01;
	tracks[3].waveFrequency = 2500.0f;			// unused
	tracks[3].decayAmount = 128;
	tracks[3].levelAmount = 32;
    tracks[3].levelMax = 100;
	tracks[3].toneAmount = 0;
    //memcpy(tracks[3].sequence, allOnSequence, SEQUENCE_LEN);

	// Low Tom
	tracks[4].waveLookupTable = waveTableTriangle;
	tracks[4].decayLookupTable =  modTableRampDown01;
	tracks[4].waveFrequency = 80.0f;
	tracks[4].decayAmount = 128;
	tracks[4].levelAmount = 192;
    tracks[4].levelMax = 255;
	tracks[4].toneAmount = 0;

	// Mid Tom
	tracks[5].waveLookupTable = waveTableSine;
	tracks[5].decayLookupTable = modTableRampDown01;
	tracks[5].waveFrequency = 1000.0f;
	tracks[5].decayAmount = 16;
	tracks[5].levelAmount = 64;
    tracks[5].levelMax = 255;
	tracks[5].toneAmount = 0;
	
	// High Tom
	tracks[6].waveLookupTable = waveTableSine;
	tracks[6].decayLookupTable = modTableSustainBeforeRampDown01;
	tracks[6].waveFrequency = 2000.0f;
	tracks[6].decayAmount = 16;
	tracks[6].levelAmount = 64;
    tracks[6].levelMax = 255;
	tracks[6].toneAmount = 0;

	// Rimshot
	tracks[7].waveLookupTable = waveTableSawUp;
	tracks[7].decayLookupTable = modTableRampDown01;
	tracks[7].waveFrequency = 1000.0f;
	tracks[7].decayAmount = 64;
	tracks[7].levelAmount = 64;
    tracks[7].levelMax = 255;
	tracks[7].toneAmount = 0;
}

//=================================================
// ノイズ生成
//
//=================================================
uint16_t generateNoise()
{
    return rand() % (UINT16_MAX + 1);    
}

uint16_t generateFilteredNoise()
{
    uint16 r, fr;
    
    //r = PRS_1_Read();    
    r = rand() % (UINT16_MAX + 1);
    
    /* Enable the interrupt register bit to poll
       Value 1 for Channel A, Value 2 for Channel B */
    Filter_INT_CTRL_REG |= (1 << Filter_CHANNEL_A);
          
    Filter_Write16(Filter_CHANNEL_A, r);
    
    /* Poll waiting for the holding register to have data to read */
    while (Filter_IsInterruptChannelA() == 0) ;
    
    fr = Filter_Read16(Filter_CHANNEL_A);
    
    //UART_printf("%u\r\n", fr);   
    
    return fr;
}

//=================================================
// Sync信号出力
//
//=================================================
void generateSyncSignal()
{
    Pin_ISR_Check_Write(1u);
    CyDelay(1);
    Pin_ISR_Check_Write(0u);
}

//=================================================
// メインルーチン
//
//=================================================
int main()
{
    // パラメータの初期化
    initTracks(tracks);
    initDDSParameter(tracks);
    setNoiseGenFuncWhite(generateNoise);
    setNoiseGenFuncBule(generateFilteredNoise);
    setNoteUpdeteFunc(generateSyncSignal);
    
    // LCDを初期化
    LCD_Char_Start();
    LCD_printf(0, TITLE_STR);
    LCD_printf(1, VERSION_STR);
    
    // LED Check
    RGB_LED_ON_RED;
    CyDelay(500);
    RGB_LED_ON_GREEN;
    CyDelay(500);
    RGB_LED_ON_BLUE;
    CyDelay(500);
    RGB_LED_OFF;
  
    // Sequencerをリセット
    Pin_Sequencer_Reset_Write(0u);
    CyDelay(1);
    Pin_Sequencer_Reset_Write(1u);
    
    // Sequencerを初期化
    I2CM_Sequencer_Start();
    
    // Sampling Timerを初期化
    Timer_Sampling_Start();
    ISR_Timer_Sampling_StartEx(Timer_Sampling_interrupt_handler);
    
    // Filterを初期化
    Filter_Start();
    
    // SPI Masterを初期化
    //SPIM_Start();
    
    CyGlobalIntEnable;
    
    // Sequencerの初期化待ち
    CyDelay(2000);
    
    // VDAC8を初期化
    VDAC8_1_Start();
    VDAC8_2_Start();
    
    // Opampを初期化
    Opamp_1_Start();
    Opamp_2_Start();
    
    for(;;)
    {
        // Read from sequencer board
        //
        if (readSequencerBoard() != SEQUENCER_TRANSFER_CMPLT) {
            error(ERR_SEQUENCER_READ, sequencerRdStatus);
        };
        
        // Write to sequencer board
        //
        if (writeSequencerBoard() != SEQUENCER_TRANSFER_CMPLT) {
            error(ERR_SEQUENCER_WRITE, sequencerWrStatus);
        }            
        
        // Read Rotary Encoder
        readDecay();
        readLevel();
        readTone();
        
        // Read tact switches
        readTactSwitch();
        /*
        LCD_printf(1, "%x: %d %d ",
            tactSwitch,
            tactSwitch & (1 << TACT_SW1),
            tactSwitch & (1 << TACT_SW2)
        );
        */
        
        if (isREDirty & (1 << RE_TONE)) {
            setWaveDDSParameter(&tracks[sequencerRdBuffer.track]);
        }
        
        setTrack(tracks, sequencerRdBuffer.track, &sequencerRdBuffer);
        
        sequencerWrBuffer[0] = getNoteCount() % 16;

        displaySequencerParameter();
                
        CyDelay(1);
    }
}

/* [] END OF FILE */

/*
 *  MPC-1000 BC 
 *
 *  Created on  : 2014-02-13
 *  Authors     : Mike J, Bahar S, Ehsan A, Bojan S
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include "altera_up_avalon_video_pixel_buffer_dma.h"
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "sys/alt_alarm.h"
#include "sys/alt_timestamp.h"
#include "alt_types.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"
#include "system.h"
#include "altera_up_ps2_keyboard.h"
#include "altera_up_avalon_ps2_regs.h"
#include "altera_up_avalon_ps2.h"
#include <altera_up_sd_card_avalon_interface.h>
#include "altera_up_avalon_character_lcd.h"
#include "altera_up_avalon_audio_and_video_config.h"
#include "altera_up_avalon_audio.h"


//Audio Variables 
#define NUM_OF_MUSIC 4   //Used for averaging of sample volumes 
#define verbose 1
alt_up_sd_card_dev *sdcard_dev = NULL;              
alt_up_audio_dev *audio_dev;
void av_config_setup();
unsigned char * data;

//This variable decides how many words are taken from the .wav Samples on the SD card.
//It can be increased to alow for longer play of samples and thus a slower loop tempo
//The FPGA's ram maxes out if the sample size > 15k
#define sample_size 8000


//VGA Variables 
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

//I/O Variables,
//These were hardcoded because there QSYS addresses were locked. 
#define SWITCHES_BASE_ADDRESS 0x00004040
#define LED_BASE_ADDRESS 0x00004050


#define SDverbose 1
//Global Variable for loop grid
bool LOOP_GRID[4][16];
int ROW_SELECTOR = 0;
int COLUMN_SELECTOR = 0;

//Global variable for scroll bar position
int SCRLBAR_POS = 1;

//Global variable for audio loop position
int AUDIO_LOOP_POS = 0;

enum channels {
	mono, stereo
};

//Struct used to analyze .wav file info 
struct file_attr {
	int data[sample_size];
	int array_size;
	float ratio;
	int filehandle;
	char file_name[12];
	int size_of_file;
	int size_of_data;
	int samples_per_second;
	int bytes_per_second;
	int max_value;
	int id;
	int index_of_max_value;
	enum channels num_of_channels;
};

//Functions to handle audio interrupts and layering
void init_audio_interrupt();
void init_pio();
void handle_audio_interrupts(void* context, alt_u32 id);
void clear_row();
int fileOpen(struct file_attr * sample_attr);
int column_detector (int x_position);
int row_detector (int y_position);
void handle_buttons_interrupt(void * context, unsigned int irq_id);
void handle_switches_interrupt(void * context, unsigned int irq_id);
static void keyboard_ISR( void *context, alt_u32 id );


int min;
unsigned char * data;

//These are the buffers that each .wav file on the SD card are read into 
int wavData[sample_size];
int wavData_two[sample_size];
int wavData_three[sample_size];
int wavData_four[sample_size];

// This is the buffer that is sent to the audio codec to be played as audio.
int fifo_data[16 * sample_size];
int * fifo_data_ptr = fifo_data;

// The audio interupt edits this variable to allow for us to keep track of the amount of samples played.
// Keeping track of samples played is how the VGA UI is kept in sync with the audio playback 
int num_played_samples = 0;


int * con_ptrr = (int*) wavData;
int * con_ptrr_two = (int*) wavData_two;
int total_num_samples = 0;


// PS/2 keyboard variables to keep track of position on the screen 
int x_0, x_1, y_0, y_1;
x_0 = 1;
x_1 = 19;
y_0 = 1;
y_1 = 1;
int up_once = 0;
int left_once = 0;
int right_once = 0;
int down_once = 0;
int space_once = 0;
volatile int edge_capture; // A variable to hold the value of the button pio edge capture register.
int count = 0;
alt_up_pixel_buffer_dma_dev* pixel_buffer;
alt_up_ps2_dev *ps2;
KB_CODE_TYPE decode_mode;
char ascii;
alt_u8 buf[4];


//Functions for external I/O init
static void init_buttons_pio();
static void init_switches_pio();

int* x_pos; //Points to the global variable SCRLBAR_POS


//Functions in charge of managing the VGA UI 
void draw_loop_grid(alt_up_pixel_buffer_dma_dev* pixel_buffer);
void draw_scroll_bar(int *x_pos);
void fill_loop_grid(int row_select, int column_select);

//Function that handles the playing of audio 
void play_audio(int *x_pos, alt_up_audio_dev *audio_dev);


//Initialization functions
init_audio_hal();
init_SD_Card_hal();


int main() {
    
	int i, j;
	int n = 0;
	*x_pos = 1;

	av_config_setup();

	init_audio_hal();

    init_SD_Card_hal();

    //Gather information about what is contained on the SD Card now that it is initialized
    
    int num_of_files =0;
	char buffer_name[10];
	int handler = 0;
    
	handler = alt_up_sd_card_find_first(" ", buffer_name);
	while ((handler =alt_up_sd_card_find_next(buffer_name) ) == !(-1)) {
			num_of_files++;
	}
    
	printf ("There are %d number of files in the SD card.\n",num_of_files);
    
	handler=0;
	int file_counter=0;
	char file_names [num_of_files][10];
	handler = alt_up_sd_card_find_first(" ", file_names[file_counter] );
	file_counter++;
	while ((handler =alt_up_sd_card_find_next (file_names[file_counter] ) ) == !(-1)) {
		file_counter++;
	}
	printf("Name of %d number of files were saved.\n",file_counter);
	int print_count =0 ;
	for (print_count=0; print_count<num_of_files;print_count++)
	{
		printf("Name of the %d file is %s.\n", print_count,file_names[print_count]);
	}

    //Variables to hold information about the samples that will be used. 
	struct file_attr sample0_wav_file; 
	struct file_attr sample1_wav_file;
	struct file_attr sample2_wav_file;
	struct file_attr sample3_wav_file;

	//Ask the user which samples they would like to use
	printf("Please enter the name of the first sample file. \n");
	scanf("%s", sample0_wav_file.file_name);
	printf("Please enter the name of the sample 2 file. \n");
	scanf("%s", sample1_wav_file.file_name);
	printf("Please enter the name of the sample 3 file. \n");
	scanf("%s", sample2_wav_file.file_name);
	printf("Please enter the name of the sample 4 file. \n");
	scanf("%s", sample3_wav_file.file_name);

	//attempt to open first sample  
	// Note that the struct sample0_wav_file has the information about each wac file like their name, 
		//pointer to data, size and ... 
	//Opening the wav file with the name that is stored in sample0_wav_file.file_name
	sample0_wav_file.filehandle = alt_up_sd_card_fopen(
			sample0_wav_file.file_name, false);
	//Checking to see if the file is opened successfully. 
	if (sample0_wav_file.filehandle == -1)
		printf("The file could not be opened.\n");
	else if (sample0_wav_file.filehandle == -2)
		printf("The file is already open.\n");
	else
		printf("File is opened successfully. waveFile=%d.\n",
				sample0_wav_file.filehandle);

	// In this section actual data from the file is read
	short sample1;
	short sample2;
	short sample;
	sample0_wav_file.max_value = 0;
	while (1) {
		// The function alt_up_sd_card_read() returns a byte of data from wav file,
		sample1 = alt_up_sd_card_read(sample0_wav_file.filehandle);
		sample2 = alt_up_sd_card_read(sample0_wav_file.filehandle);

		//The two bytes saved in sample1 and sample2 have to be concatenated and made up a word size of data 
			//stored in and int type.
		//The reason it has to be stored in int is that the function which writes to fifo_space takes in an int type.  
		sample = (short) ((unsigned char) (sample2 & 0x00FF) << 8)
				| (unsigned char) (sample & 0xFF);
		wavData[n] = (int) sample;

		//the first 44 bytes are header info. So they should not be included. 
		if (wavData[n] > sample0_wav_file.max_value && n > 23) {
			sample0_wav_file.index_of_max_value = n;
			sample0_wav_file.max_value = wavData[n];
		}
		n++;
		total_num_samples++;

		//This is just to check that the data is loaded. 
		if (total_num_samples % 1000 == 0) {
			printf("samples:%d\n", total_num_samples);
		}
		
		//Each sample has to be 11000 word so we need to break if we have read that amount of data. 
		if (total_num_samples >= sample_size)
			break;

	}

	// Now we how much data is in array
	//Updating the struct that hold information about this wav file. 
	sample0_wav_file.array_size = n;
    
	//Close wav file 0 that audio was taken from
	int state;
	state = alt_up_sd_card_fclose(sample0_wav_file.filehandle);
	//Checking to see if the file is closed. 
	if (state == 1)
		printf("The file closed successfully.\n");
	else
		printf("File is was not closed successfully.\n");

	printf("Adjusting the sample data.\n");
	printf("Maximum value of original data for sample0 is %d at %d\n",
			sample0_wav_file.max_value, sample0_wav_file.index_of_max_value);
	printf("Number of data for sample0: %d\n", sample0_wav_file.array_size);

	//We have 4 wav files therefore at worst case we might that we add 4 samples and as a result overflow occurs.
	//Also, some of the files has a higher volume, with this procedure (below) we scale all the file into same level. 
	
	//This figures out the ratio each data has to be multiplies to 
	//maximum possible value is 32767 which has to be divided equally among 4 wav files.  
	//sample0_wav_file.ratio hold the ratio. 
	if (sample0_wav_file.max_value > (32767 / NUM_OF_MUSIC))
		sample0_wav_file.ratio = (float) (32767 / NUM_OF_MUSIC)
				/ (float) sample0_wav_file.max_value;
	else
		sample0_wav_file.ratio = (float) sample0_wav_file.max_value
				/ (float) (32767 / NUM_OF_MUSIC);

	//Here we scale down or up all the data for each sound sample by the ratio that is saved on the file's struct.	
	printf("The ratio for sample 0 is : %f\n", sample0_wav_file.ratio);
	for (n = 0; n < sample0_wav_file.array_size; n++) {
		//note that float is multiplied by int, and the int value is saved. 
		wavData[n] = (float) wavData[n] * sample0_wav_file.ratio;
		if (n % 100 == 0 && SDverbose)
			printf("wavData[%d]=%d\n", n, wavData[n]);
	}
	printf("New max value sample0 is %d.\n\n",
			wavData[sample0_wav_file.index_of_max_value]);

			
	// Same procedure is done to the rest 3 wav files. 
	//Now read from sample 2
	sample1_wav_file.max_value = 0;
	sample1_wav_file.filehandle = alt_up_sd_card_fopen(
			sample1_wav_file.file_name, false);
	if (sample1_wav_file.filehandle == -1)
		printf("The file could not be opened.\n");
	else if (sample1_wav_file.filehandle == -2)
		printf("The file is already open.\n");
	else
		printf("File is opened successfully. waveFile=%d.\n",
				sample1_wav_file.filehandle);

	int m = 0;
	short sample1_two;
	short sample2_two;
	short sample_two;
	total_num_samples = 0;
	while (1) {
		sample1_two = alt_up_sd_card_read(sample1_wav_file.filehandle);
		sample2_two = alt_up_sd_card_read(sample1_wav_file.filehandle);

		sample_two = (short) ((unsigned char) (sample2_two & 0x00FF) << 8)
				| (unsigned char) (sample_two & 0xFF);
		wavData_two[m] = (int) sample_two;

		// the first 44 bytes are header so m>23 has to be considered
		if (wavData_two[m] > sample1_wav_file.max_value && m > 23) {
			sample1_wav_file.index_of_max_value = m;
			sample1_wav_file.max_value = wavData_two[m];
		}
		m++;

		total_num_samples++;

		if (total_num_samples % 1000 == 0) {
			printf("samples_two:%d\n", total_num_samples);
		}
		if (total_num_samples >= sample_size)
			break;
	}

	// Now we how much data is in array
	sample1_wav_file.array_size = m;
	state = alt_up_sd_card_fclose(sample1_wav_file.filehandle);
	if (state == 1)
		printf("The file closed successfully.\n");
	else
		printf("File is was not closed successfully.\n");

	printf("Adjusting the sample 1 data.\n");
	printf("Maximum value of original data for sample1 is %d at %d\n",
			sample1_wav_file.max_value, sample1_wav_file.index_of_max_value);
	printf("Number of data for sample1: %d\n", sample1_wav_file.array_size);

	//This figures out the ration each data has to be multiplies to
	if (sample1_wav_file.max_value > (32767 / NUM_OF_MUSIC))
		sample1_wav_file.ratio = (float) (32767 / NUM_OF_MUSIC)
				/ (float) sample1_wav_file.max_value;
	else
		sample1_wav_file.ratio = (float) sample1_wav_file.max_value
				/ (float) (32767 / NUM_OF_MUSIC);

	printf("The ration for sample1 is : %f\n", sample1_wav_file.ratio);
	for (m = 0; m < sample1_wav_file.array_size; m++) {
		wavData_two[m] = (float) wavData_two[m] * sample1_wav_file.ratio;
		if (m % 100 == 0)
			printf("wavData_two[%d]=%d\n", m, wavData_two[m]);
	}
	printf("New max value sample1 is: wavData_two[%d] = %d.\n\n",
			sample1_wav_file.index_of_max_value,
			wavData_two[sample1_wav_file.index_of_max_value]);

	//////////////////////////////////////////////////
	//Read sample 3

	sample2_wav_file.max_value = 0;
	sample2_wav_file.filehandle = alt_up_sd_card_fopen(
			sample2_wav_file.file_name, false);
	if (sample2_wav_file.filehandle == -1)
		printf("The file could not be opened.\n");
	else if (sample2_wav_file.filehandle == -2)
		printf("The file is already open.\n");
	else
		printf("File is opened successfully. waveFile=%d.\n",
				sample2_wav_file.filehandle);

	m = 0;
	short sample1_three;
	short sample2_three;
	short sample_three;
	total_num_samples = 0;
	while (1) {
		sample1_three = alt_up_sd_card_read(sample2_wav_file.filehandle);
		sample2_three = alt_up_sd_card_read(sample2_wav_file.filehandle);

		sample_three = (short) ((unsigned char) (sample2_three & 0x00FF) << 8)
				| (unsigned char) (sample_three & 0xFF);
		wavData_three[m] = (int) sample_three;

		// the first 44 bytes are header so m>23 has to be considered
		if (wavData_three[m] > sample2_wav_file.max_value && m > 23) {
			sample2_wav_file.index_of_max_value = m;
			sample2_wav_file.max_value = wavData_three[m];
		}
		m++;

		total_num_samples++;

		if (total_num_samples % 1000 == 0) {
			printf("samples_three:%d\n", total_num_samples);
		}
		if (total_num_samples >= sample_size)
			break;
	}
	// Now we how much data is in array
	sample2_wav_file.array_size = m;
	state = alt_up_sd_card_fclose(sample2_wav_file.filehandle);
	if (state == 1)
		printf("The file closed successfully.\n");
	else
		printf("File is was not closed successfully.\n");

	printf("Adjusting the sample 2 data.\n");
	printf("Maximum value of original data for sample2 is %d at %d\n",
			sample2_wav_file.max_value, sample2_wav_file.index_of_max_value);
	printf("Number of data for sample2: %d\n", sample2_wav_file.array_size);

	//This figures out the ration each data has to be multiplies to
	if (sample2_wav_file.max_value > (32767 / NUM_OF_MUSIC))
		sample2_wav_file.ratio = (float) (32767 / NUM_OF_MUSIC)
				/ (float) sample2_wav_file.max_value;
	else
		sample2_wav_file.ratio = (float) sample2_wav_file.max_value
				/ (float) (32767 / NUM_OF_MUSIC);

	printf("The ration for sample2 is : %f\n", sample2_wav_file.ratio);
	for (m = 0; m < sample2_wav_file.array_size; m++) {
		wavData_three[m] = (float) wavData_three[m] * sample2_wav_file.ratio;
		if (m % 100 == 0)
			printf("wavData_three[%d]=%d\n", m, wavData_three[m]);
	}
	printf("New max value sample1 is: wavData_two[%d] = %d.\n\n",
			sample2_wav_file.index_of_max_value,
			wavData_three[sample2_wav_file.index_of_max_value]);
	//././././././././././././././././././././././././.

	//Read sample 4
	sample3_wav_file.max_value = 0;
	sample3_wav_file.filehandle = alt_up_sd_card_fopen(
			sample3_wav_file.file_name, false);
	if (sample3_wav_file.filehandle == -1)
		printf("The file could not be opened.\n");
	else if (sample3_wav_file.filehandle == -2)
		printf("The file is already open.\n");
	else
		printf("File is opened successfully. waveFile=%d.\n",
				sample3_wav_file.filehandle);

	m = 0;
	short sample1_four;
	short sample2_four;
	short sample_four;
	total_num_samples = 0;
	while (1) {
		sample1_four = alt_up_sd_card_read(sample3_wav_file.filehandle);
		sample2_four = alt_up_sd_card_read(sample3_wav_file.filehandle);

		sample_four = (short) ((unsigned char) (sample2_four & 0x00FF) << 8)
				| (unsigned char) (sample_four & 0xFF);
		wavData_four[m] = (int) sample_four;

		// the first 44 bytes are header so m>23 has to be considered
		if (wavData_four[m] > sample3_wav_file.max_value && m > 23) {
			sample3_wav_file.index_of_max_value = m;
			sample3_wav_file.max_value = wavData_four[m];
		}
		m++;

		total_num_samples++;

		if (total_num_samples % 1000 == 0) {
			printf("samples_four:%d\n", total_num_samples);
		}
		if (total_num_samples >= sample_size)
			break;
	}
	printf("Total_num_samples: %d\n", total_num_samples);

	// Now we how much data is in array
	sample3_wav_file.array_size = m;
	state = alt_up_sd_card_fclose(sample3_wav_file.filehandle);
	if (state == 1)
		printf("The file closed successfully.\n");
	else
		printf("File is was not closed successfully.\n");

	printf("Adjusting the sample 3 data.\n");
	printf("Maximum value of original data for sample3 is %d at %d\n",
			sample3_wav_file.max_value, sample3_wav_file.index_of_max_value);
	printf("Number of data for sample3: %d\n", sample3_wav_file.array_size);

	//This figures out the ration each data has to be multiplies to
	if (sample3_wav_file.max_value > (32767 / NUM_OF_MUSIC))
		sample3_wav_file.ratio = (float) (32767 / NUM_OF_MUSIC)
				/ (float) sample3_wav_file.max_value;
	else
		sample3_wav_file.ratio = (float) sample3_wav_file.max_value
				/ (float) (32767 / NUM_OF_MUSIC);

	printf("The ration for sample3 is : %f\n", sample3_wav_file.ratio);
	for (m = 0; m < sample3_wav_file.array_size; m++) {
		wavData_four[m] = (float) wavData_four[m] * sample3_wav_file.ratio;
		if (m % 100 == 0)
			printf("wavData_four[%d]=%d\n", m, wavData_four[m]);
	}
	printf("New max value sample3 is: wavData_two[%d] = %d.\n\n",
			sample3_wav_file.index_of_max_value,
			wavData_four[sample3_wav_file.index_of_max_value]);


	alt_up_audio_reset_audio_core(audio_dev);

	init_audio_interrupt();
    
    init_VGA_hal();
    
    init_buttons_pio();
    
	init_switches_pio();
    
    draw_loop_grid(pixel_buffer);

	//This box is used by keyboard.
	alt_up_pixel_buffer_dma_draw_box(pixel_buffer, x_0, y_0, x_1, y_1, 0xFFFF, 0);

	//SET UP KEYBOARD INTERRUPT//
	ps2 = alt_up_ps2_open_dev("/dev/ps2_0");
	alt_up_ps2_init(ps2);
	alt_up_ps2_clear_fifo(ps2);
	void* keyboard_control_register_ptr = (void*) (PS2_0_BASE + 4);
	alt_irq_register(PS2_0_IRQ, keyboard_control_register_ptr, keyboard_ISR);
	alt_up_ps2_enable_read_interrupt(ps2);


    // Initialize the logical loop grid 
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 16; j++) {
			LOOP_GRID[i][j] = false;
		}
	}

	while (1) {

		draw_scroll_bar(x_pos);

	}
	return 0;
	
}

void draw_scroll_bar(int* x_pos) {

	int i;

	// At each of the 16 columns, consider weather the user desires to have a sample placed there
    // and construct the corresponding part of the buffer (fifo_data) to be played back by the
    // audio codec accordingly.
    
	if (*x_pos == 1) {
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, 301, SCREEN_HEIGHT - 4,
				318, SCREEN_HEIGHT - 1, 0x0000, 0); //errase tracker box

		for (i = 0; i < sample_size; i++) {
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));//4

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));//8

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));//10

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i]))); //13

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

			i++;
			fifo_data[sample_size + i] = (((LOOP_GRID[0][1] * wavData[i])
					+ (LOOP_GRID[1][1] * wavData_two[i]) + (LOOP_GRID[2][1]
					* wavData_three[i]) + (LOOP_GRID[3][1] * wavData_four[i])));

		}

	}

	//column 2
	if (*x_pos == 21) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box

		for (i = 0; i < sample_size; i++) {
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));//3

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));//6

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));//9

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));//12

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));//15

			i++;
			fifo_data[2 * sample_size + i] = (((LOOP_GRID[0][2] * wavData[i])
					+ (LOOP_GRID[1][2] * wavData_two[i]) + (LOOP_GRID[2][2]
					* wavData_three[i]) + (LOOP_GRID[3][2] * wavData_four[i])));

		}

	}

	//column3
	if (*x_pos == 41) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box

		for (i = 0; i < sample_size; i++) {
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));//1

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i]))); //4

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));//7

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));//10

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));//13

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));

			i++;
			fifo_data[3 * sample_size + i] = (((LOOP_GRID[0][3] * wavData[i])
					+ (LOOP_GRID[1][3] * wavData_two[i]) + (LOOP_GRID[2][3]
					* wavData_three[i]) + (LOOP_GRID[3][3] * wavData_four[i])));//16


		}
	}

	//col 4
	if (*x_pos == 61) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box

		for (i = 0; i < sample_size; i++) {
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));//3

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));//6

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));//9

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));//12

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));//15

			i++;
			fifo_data[4 * sample_size + i] = (((LOOP_GRID[0][4] * wavData[i])
					+ (LOOP_GRID[1][4] * wavData_two[i]) + (LOOP_GRID[2][4]
					* wavData_three[i]) + (LOOP_GRID[3][4] * wavData_four[i])));

		}
	}

	//col 5
	if (*x_pos == 81) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box

		for (i = 0; i < sample_size; i++) {
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//1

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//4

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//5

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//8

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//10

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//11

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//13

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//15

			i++;
			fifo_data[5 * sample_size + i] = (((LOOP_GRID[0][5] * wavData[i])
					+ (LOOP_GRID[1][5] * wavData_two[i]) + (LOOP_GRID[2][5]
					* wavData_three[i]) + (LOOP_GRID[3][5] * wavData_four[i])));//16 added


		}
	}

	if (*x_pos == 101) {
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box

		for (i = 0; i < sample_size; i++) {
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));

			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));

			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));//3

			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));//6
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));//9
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));//12
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));//15
			i++;
			fifo_data[6 * sample_size + i] = (((LOOP_GRID[0][6] * wavData[i])
					+ (LOOP_GRID[1][6] * wavData_two[i]) + (LOOP_GRID[2][6]
					* wavData_three[i]) + (LOOP_GRID[3][6] * wavData_four[i])));

		}
	}

	if (*x_pos == 121) {
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box

		for (i = 0; i < sample_size; i++) {
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));//1
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));//4
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));//7
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));//10
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));//13
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));
			i++;
			fifo_data[7 * sample_size + i] = (((LOOP_GRID[0][7] * wavData[i])
					+ (LOOP_GRID[1][7] * wavData_two[i]) + (LOOP_GRID[2][7]
					* wavData_three[i]) + (LOOP_GRID[3][7] * wavData_four[i])));//16

		}
	}

	if (*x_pos == 141) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));//3
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));//6
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));//9
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));//12
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));//15
			i++;
			fifo_data[8 * sample_size + i] = (((LOOP_GRID[0][8] * wavData[i])
					+ (LOOP_GRID[1][8] * wavData_two[i]) + (LOOP_GRID[2][8]
					* wavData_three[i]) + (LOOP_GRID[3][8] * wavData_four[i])));//16

		}
	}

	if (*x_pos == 161) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));//1
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));//4
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));//7
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));//10
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));//13
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));
			i++;
			fifo_data[9 * sample_size + i] = (((LOOP_GRID[0][9] * wavData[i])
					+ (LOOP_GRID[1][9] * wavData_two[i]) + (LOOP_GRID[2][9]
					* wavData_three[i]) + (LOOP_GRID[3][9] * wavData_four[i])));//16

		}
	}

	if (*x_pos == 181) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));//3
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));//6
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));//9
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));//12
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));//15
			i++;
			fifo_data[10 * sample_size + i]
					= (((LOOP_GRID[0][10] * wavData[i]) + (LOOP_GRID[1][10]
							* wavData_two[i]) + (LOOP_GRID[2][10]
							* wavData_three[i]) + (LOOP_GRID[3][10]
							* wavData_four[i])));

		}
	}

	if (*x_pos == 201) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));//1
			i++;
			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;
			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;
			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));//4
			i++;
			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));//7
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));//10
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));//13
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));
			i++;

			fifo_data[11 * sample_size + i]
					= (((LOOP_GRID[0][11] * wavData[i]) + (LOOP_GRID[1][11]
							* wavData_two[i]) + (LOOP_GRID[2][11]
							* wavData_three[i]) + (LOOP_GRID[3][11]
							* wavData_four[i])));//16


		}
	}

	if (*x_pos == 221) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));//3
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));//6
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));//9
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;

			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));//12
			i++;
			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;
			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));
			i++;
			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));//15
			i++;
			fifo_data[12 * sample_size + i]
					= (((LOOP_GRID[0][12] * wavData[i]) + (LOOP_GRID[1][12]
							* wavData_two[i]) + (LOOP_GRID[2][12]
							* wavData_three[i]) + (LOOP_GRID[3][12]
							* wavData_four[i])));

		}
	}

	if (*x_pos == 241) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));//1
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));//4
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));//7
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));//10
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));//13
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));
			i++;
			fifo_data[13 * sample_size + i]
					= (((LOOP_GRID[0][13] * wavData[i]) + (LOOP_GRID[1][13]
							* wavData_two[i]) + (LOOP_GRID[2][13]
							* wavData_three[i]) + (LOOP_GRID[3][13]
							* wavData_four[i])));//16


		}
	}

	if (*x_pos == 261) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;
			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));//2
			i++;
			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));//5
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));//8
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));//11
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));//14
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));
			i++;

			fifo_data[14 * sample_size + i]
					= (((LOOP_GRID[0][14] * wavData[i]) + (LOOP_GRID[1][14]
							* wavData_two[i]) + (LOOP_GRID[2][14]
							* wavData_three[i]) + (LOOP_GRID[3][14]
							* wavData_four[i])));//16


		}
	}

	if (*x_pos == 281) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 18, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));//3
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));//6
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));//9
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));//12
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));//15
			i++;

			fifo_data[15 * sample_size + i]
					= (((LOOP_GRID[0][15] * wavData[i]) + (LOOP_GRID[1][15]
							* wavData_two[i]) + (LOOP_GRID[2][15]
							* wavData_three[i]) + (LOOP_GRID[3][15]
							* wavData_four[i])));

		}
	}

	if (*x_pos == 301) {

		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, *x_pos, SCREEN_HEIGHT
				- 4, *x_pos + 17, SCREEN_HEIGHT - 1, 0xD940, 0); //draw tracker box
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, (*x_pos - 20),
				SCREEN_HEIGHT - 4, (*x_pos - 20) + 18, SCREEN_HEIGHT - 1,
				0x0000, 0); //draw tracker box


		for (i = 0; i < sample_size; i++) {
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i]))); //9

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));//12

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));//15

			i++;
			fifo_data[i] = (((LOOP_GRID[0][0] * wavData[i]) + (LOOP_GRID[1][0]
					* wavData_two[i]) + (LOOP_GRID[2][0] * wavData_three[i])
					+ (LOOP_GRID[3][0] * wavData_four[i])));

		}
	}
}

void draw_loop_grid(alt_up_pixel_buffer_dma_dev* pixel_buffer) {

	int j;

	//Columns
	for (j = 0; j < 16; j++) {
		alt_up_pixel_buffer_dma_draw_vline(pixel_buffer, (j * (SCREEN_WIDTH
				/ 16)), 239, 0, 0xFFFF, 0);
		printf("Column Position %d : %d\n", j, (j * (SCREEN_WIDTH / 16)));

	}

	//Last Column isn't drawing so manually adding it in
	alt_up_pixel_buffer_dma_draw_vline(pixel_buffer, SCREEN_WIDTH - 1, 239, 0,
			0xFFFF, 0);
	printf("Last column pos : %d\n", SCREEN_WIDTH - 1);

	//Rows
	for (j = 0; j < 4; j++) {
		//draw the row bars
		alt_up_pixel_buffer_dma_draw_hline(pixel_buffer, 0, 319, (SCREEN_HEIGHT
				/ 4) * j, 0xFFFF, 0);
		printf("Row position %d : %d\n", j, (j * (SCREEN_HEIGHT / 4)));

	}
	//Last row isn't drawing so manually adding it in
	alt_up_pixel_buffer_dma_draw_hline(pixel_buffer, 0, 319, SCREEN_HEIGHT - 1,
			0xFFFF, 0);
	printf("Last row pos: %d\n", SCREEN_HEIGHT - 1);

}


// This function is called in response to a user placing or removing a switch so that the
// VGA display reacts in real time to their input.
void fill_loop_grid(int row_select, int column_select) {

    // If the specified loop grid position is false, the user wants it to be true, so change it from black to green.
    // Otherwise, the space is true and the user wants the sample to be removed, or in other words changed
    // from black to green.  
    
	if (LOOP_GRID[row_select][column_select] == 0) {
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, ((SCREEN_WIDTH / 16)
				* column_select) + 1,
				((SCREEN_HEIGHT / 4) * row_select + 1),
				((SCREEN_WIDTH / 16) * (column_select + 1) - 1),
				((SCREEN_HEIGHT / 4) * (row_select + 1) - 1), 0xA700, 0);
	} else if (LOOP_GRID[row_select][column_select] > 0) {
		alt_up_pixel_buffer_dma_draw_box(pixel_buffer, ((SCREEN_WIDTH / 16)
				* column_select) + 1,
				((SCREEN_HEIGHT / 4) * row_select + 1),
				((SCREEN_WIDTH / 16) * (column_select + 1) - 1),
				((SCREEN_HEIGHT / 4) * (row_select + 1) - 1), 0x0000, 0);
	}

}

void av_config_setup() {
	alt_up_av_config_dev * av_config = alt_up_av_config_open_dev(
			"/dev/audio_video");
	if (!av_config)
		printf("error openning\n");
	while (!alt_up_av_config_read_ready(av_config)) {
	}
}

// This function allows the user to select which sample they can place/remove
// Since each row on the VGA corrisponds to a sample, pushing a button selects
// the row the user can change. 
void handle_buttons_interrupt(void * context, unsigned int irq_id) {

	int value = IORD(BUTTONS_BASE, 3);
	int i = 0;

	printf("Intrrupt button value: %d \n\n", value);
	if (value == 0x01) {
		ROW_SELECTOR = 0;
	} else if (value == 0x02) {
		ROW_SELECTOR = 1;
	} else if (value == 0x04) {
		ROW_SELECTOR = 2;
	} else if (value == 0x08) {
		ROW_SELECTOR = 3;
	}

	for (i = 0; i < 16; i++) {
		*(red_leds_temp) = (*red_leds & (1 << i));
		*(red_leds_temp) = LOOP_GRID[ROW_SELECTOR][i];

		printf("led address %d: %d\n", i, *red_leds_temp);
	}

	IOWR(BUTTONS_BASE, 3, 0x0F);
}

int fileOpen(struct file_attr * sample_attr) {
	(*sample_attr).filehandle = alt_up_sd_card_fopen((*sample_attr).file_name,
			false);
	if ((*sample_attr).filehandle == -1) {
		printf("The file could not be opened.\n");
		return 0;
	} else if ((*sample_attr).filehandle == -2) {
		printf("The file is already open.\n");
		return 0;
	} else {
		printf("File is opened successfully. waveFile=%d.\n",
				(*sample_attr).filehandle);
	}
	int n = 0;
	int total_num_samples = 0;
	short sample1;
	short sample2;
	short sample;
	(*sample_attr).max_value = 0;
	while (1) {
		sample1 = alt_up_sd_card_read((*sample_attr).filehandle);
		sample2 = alt_up_sd_card_read((*sample_attr).filehandle);

		sample = (short) ((unsigned char) (sample2 & 0x00FF) << 8)
				| (unsigned char) (sample & 0xFF);
		(*sample_attr).data[n] = (int) sample;

		if ((*sample_attr).data[n] > (*sample_attr).max_value && n > 23) {
			(*sample_attr).index_of_max_value = n;
			(*sample_attr).max_value = (*sample_attr).data[n];
		}
		n++;
		total_num_samples++;

		if (total_num_samples % 1000 == 0) {
			printf("%s samples:%d\n", (*sample_attr).file_name,
					total_num_samples);
		}
		if (total_num_samples >= sample_size)
			break;

	}

	(*sample_attr).array_size = n;

	if (alt_up_sd_card_fclose((*sample_attr).filehandle))
		printf("The file %s closed successfully.\n", (*sample_attr).file_name);
	else
		printf("File is was not closed successfully.\n");

	printf("Adjusting the sample data of %s.\n", (*sample_attr).file_name);
	printf("Maximum value of original data for sample0 is %d at %d\n",
			(*sample_attr).max_value, (*sample_attr).index_of_max_value);
	printf("Number of data for sample %s: %d\n", (*sample_attr).file_name,
			(*sample_attr).array_size);

	if ((*sample_attr).max_value > (32767 / NUM_OF_MUSIC))
		(*sample_attr).ratio = (float) (32767 / NUM_OF_MUSIC)
				/ (float) (*sample_attr).max_value;
	else
		(*sample_attr).ratio = (float) (*sample_attr).max_value
				/ (float) (32767 / NUM_OF_MUSIC);

	printf("The ration for sample %s is : %f\n", (*sample_attr).file_name,
			(*sample_attr).ratio);
	for (n = 0; n < (*sample_attr).array_size; n++) {
		(*sample_attr).data[n] = (float) (*sample_attr).data[n]
				* (*sample_attr).ratio;
		if (n % 100 == 0 && SDverbose)
			printf("(*sample_attr).data[%d]=%d\n", n, (*sample_attr).data[n]);
	}
	printf("New max value sample %s is %d.\n\n", (*sample_attr).file_name,
			(*sample_attr).data[(*sample_attr).index_of_max_value]);
	return 1;
}

// This interrupt is triggered on a switch activity. It then changes the corrisponding
// position in the logical loop grid to the opposite of what it currently is (from true
// to false or vise versa). It also calls the fill_loop_grid function so that the VGA
// is updated in real time.
void handle_switches_interrupt(void * context, unsigned int irq_id) {
	printf("Trying to handly interrupts 3 \n\n");

	int value = IORD(SWITCHES_BASE, 3);
	printf("Switch Value: %d\n\n", value);
	if (value == 0x04) {
		COLUMN_SELECTOR = 15;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];

	} else if (value == 0x08) {
		COLUMN_SELECTOR = 14;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 16) {
		COLUMN_SELECTOR = 13;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 32) {
		COLUMN_SELECTOR = 12;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 64) {
		COLUMN_SELECTOR = 11;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 128) {
		COLUMN_SELECTOR = 10;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 256) {
		COLUMN_SELECTOR = 9;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 512) {
		COLUMN_SELECTOR = 8;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 1024) {
		COLUMN_SELECTOR = 7;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 2048) {
		COLUMN_SELECTOR = 6;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 4096) {
		COLUMN_SELECTOR = 5;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 8192) {
		COLUMN_SELECTOR = 4;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 16384) {
		COLUMN_SELECTOR = 3;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 32768) {
		COLUMN_SELECTOR = 2;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 65536) {
		COLUMN_SELECTOR = 1;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 131072) {
		COLUMN_SELECTOR = 0;
		fill_loop_grid(ROW_SELECTOR, COLUMN_SELECTOR);
		LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR]
				= !LOOP_GRID[ROW_SELECTOR][COLUMN_SELECTOR];
	} else if (value == 2) {
		clear_row();
	}

	IOWR(SWITCHES_BASE, 3, 0xFFFFF);
}

void init_audio_interrupt() {
	/* Register the interrupt handler. */
#ifdef ALT_ENHANCED_INTERRUPT_API_PRESENT
	alt_ic_isr_register(0, AUDIO_IRQ, handle_audio_interrupts, null, NULL);
	alt_ic_irq_enable(0, AUDIO_IRQ);
#else
	alt_irq_register(AUDIO_IRQ, NULL, handle_audio_interrupts);
	alt_up_audio_enable_write_interrupt(audio_dev);
#endif
}

void handle_audio_interrupts(void* context, alt_u32 id) {

	int spaceR;
	int spaceL;
	//The available space in right fifo_space and right fifo_space is calculated.  
	spaceR = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
	spaceL = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_LEFT);

	//find the min value 
	min = (spaceR < spaceL) ? spaceR : spaceL;

	//Write min amout of data into fifo_space. 
	alt_up_audio_write_fifo(audio_dev, fifo_data_ptr, min, ALT_UP_AUDIO_RIGHT);
	alt_up_audio_write_fifo(audio_dev, fifo_data_ptr, min, ALT_UP_AUDIO_LEFT);
	
	//Move the pointer based on min. 
	fifo_data_ptr += min;

	//Adding all the min values so that we know how much data is played. 
	num_played_samples += min;
	
	//We have 16 bars. Each bar takes 11000 data to be played. 
	//we want to go back to the first bar when 16 bars are played. 
	if (num_played_samples > 16 * sample_size) {
		fifo_data_ptr = fifo_data;
		num_played_samples = 0;
	}
	
	// num_played_samples is 11000*16 and sample_size = 11000
	// We update the x_pos based on how much data is played. 
	// For example if less than 11000 data is played then we know we are still playing first bar.  
	// If this If condition is true we know that we have played all of the data for last bar and therefore 
		//we move to first bar. 
	if (num_played_samples / (sample_size) == 0 && (!(*x_pos == 1))) {
		*x_pos = 1;

	}
	//Here we are checking to see if the we are  still playing second bar or not.  
	// If this If condition is true we know that we have played all of the data for first bar and therefore 
		//we move to second bar.
	if (num_played_samples / (sample_size) == 1 && (!(*x_pos == 21))) {
		*x_pos = 21;

	}
	// If this If condition is true we know that we have played all of the data for second bar and therefore 
		//we move to third bar. 
	if (num_played_samples / (sample_size) == 2 && (!(*x_pos == 41))) {
		*x_pos = 41;

	}

	// and goes on... 
	if (num_played_samples / (sample_size) == 3 && (!(*x_pos == 61))) {
		*x_pos = 61;

	}
	if (num_played_samples / (sample_size) == 4 && (!(*x_pos == 81))) {
		*x_pos = 81;

	}
	if (num_played_samples / (sample_size) == 5 && (!(*x_pos == 101))) {
		*x_pos = 101;

	}
	if (num_played_samples / (sample_size) == 6 && (!(*x_pos == 121))) {
		*x_pos = 121;

	}
	if (num_played_samples / (sample_size) == 7 && (!(*x_pos == 141))) {
		*x_pos = 141;

	}
	if (num_played_samples / (sample_size) == 8 && (!(*x_pos == 161))) {
		*x_pos = 161;

	}
	if (num_played_samples / (sample_size) == 9 && (!(*x_pos == 181))) {
		*x_pos = 181;

	}
	if (num_played_samples / (sample_size) == 10 && (!(*x_pos == 201))) {
		*x_pos = 201;

	}
	if (num_played_samples / (sample_size) == 11 && (!(*x_pos == 221))) {
		*x_pos = 221;

	}
	if (num_played_samples / (sample_size) == 12 && (!(*x_pos == 241))) {
		*x_pos = 241;

	}
	if (num_played_samples / (sample_size) == 13 && (!(*x_pos == 261))) {
		*x_pos = 261;

	}
	if (num_played_samples / (sample_size) == 14 && (!(*x_pos == 281))) {
		*x_pos = 281;

	}
	if (num_played_samples / (sample_size) == 15 && (!(*x_pos == 301))) {
		*x_pos = 301;

	}
}

void clear_row() {
	int i;
	for (i = 0; i < 16; i++) {
		if (LOOP_GRID[ROW_SELECTOR][i] == true) {
			fill_loop_grid(ROW_SELECTOR, i);
			LOOP_GRID[ROW_SELECTOR][i] = !LOOP_GRID[ROW_SELECTOR][i];
		}
	}
}

// Every time a key is pressed on the keyboard, this interrupt service routine is called.
// The arrows keys are mapped to keys 8, 4, 6 and 2 as up, left, right and down respectively. We move our cursor accordingly after each key press.
// The space key either select or deselects the current cell based on the current "on/off" state of the cell.
static void keyboard_ISR( void *context, alt_u32 id ) {

	decode_scancode(ps2,&decode_mode, buf, &ascii);

	int column = column_detector(x_0);
	int row = row_detector(y_0);

	printf("column: %d\nrow: %d\n", column, row);

	if (decode_mode == KB_ASCII_MAKE_CODE) {

		if (ascii == '8' && up_once == 0) {
			printf("up arrow\n");
			//---================================---
			alt_up_pixel_buffer_dma_draw_box(pixel_buffer, x_0, y_0, x_1, y_1, 0x0000, 0);
			x_0 = x_0;
			y_0 -= 60 ;
			x_1 = x_1;
			y_1 -= 60;
			if (y_0 < 1){
				x_0 = x_0;
				x_1 = x_1;
				y_0 = 1;
				y_1 = 1;
			}
			up_once++;
		} else {
			up_once = 0;
		}

		if (ascii == '4' && left_once == 0) {
			printf("left arrow\n");
			alt_up_pixel_buffer_dma_draw_box(pixel_buffer, x_0, y_0, x_1, y_1, 0x0000, 0);
			x_0 -= 20;
			y_0 = y_0;
			x_1 -= 20;
			y_1 = y_1;
			if (x_0 < 1) {
				x_0 = 1;
				x_1 = 19;
				y_0 = y_0;
				y_1 = y_1;
			}

			left_once++;
		} else {
			left_once = 0;
		}

		if (ascii == '6' && right_once == 0) {
			printf("right arrow\n");
			alt_up_pixel_buffer_dma_draw_box(pixel_buffer, x_0, y_0, x_1, y_1, 0x0000, 0);
			x_0 += 20;
			y_0 = y_0;
			x_1 += 20;
			y_1 = y_1;
			if (x_1 > 318){
				x_0 = 301;
				x_1 = 319;
				y_0 = y_0;
				y_1 = y_1;
			}
			right_once++;
		} else {
			right_once = 0;
		}

		if (ascii == '2' && down_once == 0) {
			printf("down arrow\n");
			alt_up_pixel_buffer_dma_draw_box(pixel_buffer, x_0, y_0, x_1, y_1, 0x0000, 0);
			x_0 = x_0;
			y_0 += 60;
			x_1 = x_1;
			y_1 += 60;
			if (y_1 > 238){
				x_0 = x_0;
				x_1 = x_1;
				y_0 = 181;
				y_1 = 181;
			}

			down_once++;
		} else {
			down_once = 0;
		}
	}
	else if (decode_mode == KB_BINARY_MAKE_CODE) {
		if (buf[0] == 0x29 && space_once == 0) {
			printf("space bar\n");
            
			fill_loop_grid(row_detector(y_0), column_detector(x_0));
			LOOP_GRID[row][column] = !LOOP_GRID[row][column];

			space_once++;
		} else {
			space_once = 0;
		}
	}

	alt_up_pixel_buffer_dma_draw_box(pixel_buffer, x_0, y_0, x_1, y_1, 0xf000, 0);

}



int row_detector (int y_position){
int row = 0;
	if ((y_position > 0) && (y_position < 60))
		row = 0;
	else if ((y_position > 60) && (y_position < 120))
		row = 1;
	else if ((y_position > 120) && (y_position < 180))
		row = 2;
	else if ((y_position > 180) && (y_position < 240))
		row = 3;
	else
		row = 0;

	return row;
}

int column_detector (int x_position){
int column = 0;
	if ((x_position > 0) && (x_position < 20))
		column = 0;
	else if ((x_position > 20) && (x_position < 40))
		column = 1;
	else if ((x_position > 40) && (x_position < 60))
		column = 2;
	else if ((x_position > 60) && (x_position < 80))
		column = 3;
	else if ((x_position > 80) && (x_position < 100))
		column = 4;
	else if ((x_position > 100) && (x_position < 120))
		column = 5;
	else if ((x_position > 120) && (x_position < 140))
		column = 6;
	else if ((x_position > 140) && (x_position < 160))
		column = 7;
	else if ((x_position > 160) && (x_position < 180))
		column = 8;
	else if ((x_position > 180) && (x_position < 200))
		column = 9;
	else if ((x_position > 200) && (x_position < 220))
		column = 10;
	else if ((x_position > 220) && (x_position < 240))
		column = 11;
	else if ((x_position > 240) && (x_position < 260))
		column = 12;
	else if ((x_position > 260) && (x_position < 280))
		column = 13;
	else if ((x_position > 280) && (x_position < 300))
		column = 14;
	else if ((x_position > 300) && (x_position < 320))
		column = 15;
	else
		column = 0;

	return column;
}

/* Initialize the BUTTONS_pio. */
static void init_buttons_pio() {
    
	alt_irq_register(BUTTONS_IRQ, NULL, handle_buttons_interrupt);
    
	alt_irq_enable(BUTTONS_IRQ);
    
	IOWR(BUTTONS_BASE, 3, 0x0F);
    
	/* Enable all 4 button interrupts. */
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTONS_BASE, 0xf);
}

/* Initialize the SWITCHES_pio. */
static void init_switches_pio() {
    
	alt_irq_register(SWITCHES_IRQ, NULL, handle_switches_interrupt);
    
	alt_irq_enable(SWITCHES_IRQ);
    
	IOWR(SWITCHES_BASE, 3, 0xFFFFF);
    
	/* Enable all 4 button interrupts. */
	IOWR_ALTERA_AVALON_PIO_IRQ_MASK(SWITCHES_BASE, 0xfffff);
}

//Initialization functions from the start of main 
init_audio_hal(){
    
    //Initialising the audio IP Core HAL device driver.
	audio_dev = alt_up_audio_open_dev("/dev/audio");
	if (audio_dev == NULL)
		printf("Error: could not open audio device \n");
	else
		printf("Opened audio device \n");
    
}

init_SD_Card_hal(){
    
    //Initialising the SD Card IP Core HAL device driver.
	sdcard_dev = alt_up_sd_card_open_dev("/dev/SD_card");
    
	if (sdcard_dev == NULL) {
		printf("Could not read from the SDcard.\n");
		return 0;
	} else {
		if (!alt_up_sd_card_is_Present()) {
			printf("The SDcard is not present!\n");
			return 0;
		}
        
		else {
			if (!alt_up_sd_card_is_FAT16()) {
				printf(
                       "The SDcard is not formatted to be FAT16 and could not be read.\n");
				return 0;
			}
		}
        
	}
    
}

init_VGA_hal(){
    
    //VGA INIT
    // Use the name of your pixel buffer DMA core
    pixel_buffer = alt_up_pixel_buffer_dma_open_dev("/dev/pixel_buffer_dma");
    unsigned int pixel_buffer_addr1 = PIXEL_BUFFER_BASE;
    
    // Set the first buffer address
    alt_up_pixel_buffer_dma_change_back_buffer_address(pixel_buffer,
                                                       pixel_buffer_addr1);
    
    // Swap background and foreground buffers
    alt_up_pixel_buffer_dma_swap_buffers(pixel_buffer);
    
    // Wait for the swap to complete
    while (alt_up_pixel_buffer_dma_check_swap_buffers_status(pixel_buffer));
    
    // Clear both buffers (all pixels black)
    alt_up_pixel_buffer_dma_clear_screen(pixel_buffer, 0);
}
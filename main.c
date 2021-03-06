/******************************************************************************
 * Timer Output Compare Demo
 *
 * Description:
 *
 * This demo configures the timer to a rate of 1 MHz, and the Output Compare
 * Channel 1 to toggle PORT T, Bit 1 at rate of 10 Hz. 
 *
 * The toggling of the PORT T, Bit 1 output is done via the Compare Result Output
 * Action bits.  
 * 
 * The Output Compare Channel 1 Interrupt is used to refresh the Timer Compare
 * value at each interrupt
 * 
 * Author:
 *  Jon Szymaniak (08/14/2009)
 *  Tom Bullinger (09/07/2011)	Added terminal framework
 *
 *****************************************************************************/


// system includes
#include <hidef.h>      /* common defines and macros */
#include <stdio.h>      /* Standard I/O Library */
#include <ctype.h>
#include <stdlib.h>

// project includes
#include "types.h"
#include "derivative.h" /* derivative-specific definitions */

// Definitions

// Change this value to change the frequency of the output compare signal.
// The value is in Hz.
#define OC_FREQ_HZ    ((UINT16)10)

// Macro definitions for determining the TC1 value for the desired frequency
// in Hz (OC_FREQ_HZ). The formula is:
//
// TC1_VAL = ((Bus Clock Frequency / Prescaler value) / 2) / Desired Freq in Hz
//
// Where:
//        Bus Clock Frequency     = 2 MHz
//        Prescaler Value         = 2 (Effectively giving us a 1 MHz timer)
//        2 --> Since we want to toggle the output at half of the period
//        Desired Frequency in Hz = The value you put in OC_FREQ_HZ
//
#define BUS_CLK_FREQ  ((UINT32) 2000000)   
#define PRESCALE      ((UINT16)  2)         
#define TC1_VAL       ((UINT16)  (((BUS_CLK_FREQ / PRESCALE) / 2) / OC_FREQ_HZ))

// Boolean Definitions to make the code more readable.
#define FALSE 0
#define TRUE 1
#define MAXINPUTVALUES 1001

// Number of buckets in the histogram.
const int numberOfBuckets = 100; 

// This is a generic index.
UINT16 index = 0;

// Normally I'd use something awesome like a bool but we're stuck with this err
// limited system.
// This is used to let the program know when to capture values.
UINT16 captureValues = FALSE;

// holds the timer values captured on the rising edge.
UINT16 timerValuesUs [1001] = { 0 };

// holds the time inteval between rising edges.
UINT16 pulseIntervalsUs [1000] = { 0 };

// holds the minimum time value for each histogram bucket.
UINT16 minimumHistogramValueUs [100] = { 0 };

// holds the minimum time value for each histogram bucket.
UINT16 histogram [100] = { 0 };

// I prefer the new school method of declaring functions at the top of the file HR.
void displayResults(void);
void getMeasurements(void);
void getMoronsInput(UINT16* lowerBoundaryUs, UINT16* upperBoundaryUs);
UINT16 getUINT16Input(void);
UINT16 post_function(void);
void processTimerMeasurements(UINT16 lowerBoundaryUs, UINT16 upperBoundaryUs);

// Initializes SCI0 for 8N1, 9600 baud, polled I/O
// The value for the baud selection registers is determined
// using the formula:
//
// SCI0 Baud Rate = ( 2 MHz Bus Clock ) / ( 16 * SCI0BD[12:0] )
//--------------------------------------------------------------
void InitializeSerialPort(void)
{
    // Set baud rate to ~9600 (See above formula)
    SCI0BD = 13;          
    
    // 8N1 is default, so we don't have to touch SCI0CR1.
    // Enable the transmitter and receiver.
    SCI0CR2_TE = 1;
    SCI0CR2_RE = 1;
}


// Initializes I/O and timer settings for the demo.
//--------------------------------------------------------------       
void InitializeTimer(void)
{
  // Set the timer prescaler to %2, since the bus clock is at 2 MHz,
  // and we want the timer running at 1 MHz
  TSCR2_PR0 = 1;
  TSCR2_PR1 = 0;
  TSCR2_PR2 = 0;       
    
  // Change to an input compare. HR 
  // Enable input capture on Channel 1  
  TIOS_IOS1 = 0;
  
  
  // Set up input capture edge control to capture on a rising edge. 
  TCTL4_EDG1A = 1;
  TCTL4_EDG1B = 0;
   
  // from here down we want this code. HR.
  // Clear the input capture Interrupt Flag (Channel 1) 
  TFLG1 = TFLG1_C1F_MASK;
  
  // Enable the input capture interrupt on Channel 1;
  TIE_C1I = 1;  
  
  //
  // Enable the timer
  // 
  TSCR1_TEN = 1;
   
  //
  // Enable interrupts via macro provided by hidef.h
  //
  EnableInterrupts;
}


// Output Compare Channel 1 Interrupt Service Routine
// Refreshes TC1 and clears the interrupt flag.
//          
// The first CODE_SEG pragma is needed to ensure that the ISR
// is placed in non-banked memory. The following CODE_SEG
// pragma returns to the default scheme. This is neccessary
// when non-ISR code follows. 
//
// The TRAP_PROC tells the compiler to implement an
// interrupt funcion. Alternitively, one could use
// the __interrupt keyword instead.
// 
// The following line must be added to the Project.prm
// file in order for this ISR to be placed in the correct
// location:
//		VECTOR ADDRESS 0xFFEC OC1_isr 
#pragma push
#pragma CODE_SEG __SHORT_SEG NON_BANKED
//--------------------------------------------------------------       
void interrupt 9 OC1_isr( void )
{
   // This interrupt stores the values from the table into the array.
   // we don't want to do any calculations because we are dealing with
   // Us and want the reads to be as accurate as possible.
  
   if (captureValues == TRUE && index < MAXINPUTVALUES) 
   {
    
      timerValuesUs[index] = TC1;
      ++index;      
   }
   
   // set the interrupt enable flag for that port because it is cleared every
   // everytime an interrupt fires.
   TFLG1   =   TFLG1_C1F_MASK;
}
#pragma pop

// This function is called by printf in order to
// output data. Our implementation will use polled
// serial I/O on SCI0 to output the character.
//
// Remember to call InitializeSerialPort() before using printf!
//
// Parameters: character to output
//--------------------------------------------------------------       
void TERMIO_PutChar(INT8 ch)
{
    // Poll for the last transmit to be complete
    do
    {
      // Nothing  
    } while (SCI0SR1_TC == 0);
    
    // write the data to the output shift register
    SCI0DRL = ch;
}


// Polls for a character on the serial port.
//
// Returns: Received character
//--------------------------------------------------------------       
UINT8 GetChar(void)
{ 
  // Poll for data
  do
  {
    // Nothing
  } while(SCI0SR1_RDRF == 0);
   
  // Fetch and return data from SCI0
  return SCI0DRL;
}


// Entry point of our application code
//--------------------------------------------------------------       
void main(void)
{

  UINT8 userInput = 0;
  UINT16 lowerBoundaryUs = 0;
  UINT16 upperBoundaryUs = 0;
  
  InitializeSerialPort();
  InitializeTimer();
   
  // Post function
  
  // if we pass post run the program otherwise go home.
  if(post_function()) 
  {
     // Explain the program to the user.
     (void) printf("This fine piece of crap program will give you a histogram of 1000 rising edge\r\n");
     (void) printf("rising edge interarrival times.  It will display the results as a 100 bucket \r\n");
     (void) printf("histogram in ascening order, with the lowest arrival time for that bucket\r\n");
     (void) printf("displayed.\r\n\r\n");
  
  
     //start of main loop 
     for(;;)
     {
        // Check to see if the user wants another set of readings
        (void) printf("Press s key to capture the readings or e to end the program. ");
        userInput = GetChar();
        (void)printf("%c", userInput);;
    
        if(userInput == 's') {
          // clean out any old data in our tables.
          index = 0;
          memset(timerValuesUs, 0, sizeof(timerValuesUs));
          memset(pulseIntervalsUs, 0, sizeof(pulseIntervalsUs));
          memset(minimumHistogramValueUs, 0, sizeof(minimumHistogramValueUs));
          memset(histogram, 0, sizeof(histogram));
        
           // Get the input
           getMoronsInput(&lowerBoundaryUs, &upperBoundaryUs);
  
           // Debug code
           //(void)printf("\r\nlowerBoundaryUs  %u\r\n",  lowerBoundaryUs);
           //(void)printf("upperBoundaryUs  %u\r\n",  upperBoundaryUs);
           //(void)printf("index  %u\r\n", index);
           // end Debug code.    
 
           // get measurements when user pushes a key.  
           (void) getMeasurements();
  
           // calculate the histogram and outputs.
           (void) processTimerMeasurements(lowerBoundaryUs, upperBoundaryUs);
  
           // display results.
           (void) displayResults();
        } 
        else if(userInput == 'e'){
           // exit the program.
           break;
        }
     }
  }
  
  (void) printf("\r\n\r\nOk I'm outa here!!!\r\n\r\n");
}

//*****************************************************************************
// This unmitigated piece of crap will display the lowest value in each bucket
// of the minimumHistogramValue table and the number of entries in the
// corresponding bucket in the histogram table one value at at time.
// The user will need to press any key to see the next non-zero entry in the 
// tables.  
//
// The histogram is stored in the histogram table in the global namespace.
// The minimum values for each bucket are stored in the minimumHistogramValue table
// in the global namespace.
//
// Parameters: None
//
// Return: None.
//*****************************************************************************
void displayResults(void) 
{
  int i = 0;
  UINT8 userinput = 0;
  
  // This is debug code and will be ruthlessly commented out.
/*          
  for (i = 0; i < numberOfBuckets; ++i) 
  {
     if (histogram[i] !=0) 
     {
       (void)printf("histogram[%d]  %u\r\n", i, histogram[i]);
       (void)printf("minimumHistogramValue[%d]  %u\r\n", i, minimumHistogramValue[i]);
     }
  };
*/  
          
  // Give them the instructions
  (void)printf("Please press a key to show each histogram entry.\r\n");
  
  userinput = GetChar();
  
  (void)printf("\r\nStart of the histogram results.\r\n"); 
                 
  for (i = 0; i < numberOfBuckets; ++i) 
  {
     if (histogram[i] !=0) 
     {
       //(void)printf("histogram[%d]  %u\r\n", i, histogram[i]);
       (void)printf("minimumValue %u  histogram[%d]  %u \r\n", minimumHistogramValueUs[i], i, histogram[i]);
       userinput = GetChar();
     }
  };
  
  (void)printf("End of the histogram results..\r\n\r\n"); 
  
}


//*****************************************************************************
// This unmitigated piece of crap will set the captureValues flag to true and
// and then poll the index until we have MAXINPUTVALUES readings.
//
// The measurements are stored in the timerValuesUs table in the global namespace. 
//
// Parameters: NONE
//
// Return: NONE
//*****************************************************************************
void getMeasurements(void) 
{
  int i = 0;
  (void) printf("\r\nPress any key to capture the readings. ");
  
  if(GetChar()) 
  {
     // turn on recording the rising edge values.
     captureValues = TRUE;
     
     // clean up our output to the screen.
     (void) printf("\r\n\r\n");
  }
    
  while (index < MAXINPUTVALUES) {
  // spin in a tight loop while we collect the data.
  }
    
  // turn off recording the rising edge values.
  captureValues = FALSE;
    
  // This is debug code and will be ruthlessly commented out.  
  /* 
  for (i = 0; i < index; ++i) 
  {
       (void)printf("timerValuesUs[%d]  %u\r\n", i, timerValuesUs[i]);
  };
  */
}

//*****************************************************************************
// This unmitigated piece of crap will get the upper and lower boundaries we
// are going to use in the histogram.
//
// Parameters:
//    lowerBoundaryUs  A pointer to where the lower boundary value is to be stored.
//    upperBoundaryUs  A pointer to where the upper boundary value is to be stored.
//
// Return: A value between 0 and 65535 for each function argument.
//*****************************************************************************
void getMoronsInput(UINT16* lowerBoundaryUs, UINT16* upperBoundaryUs) 
{  
   // Get the lower range.
   (void) printf("\r\nPlease enter the lower range in microseconds. ");
   *lowerBoundaryUs = getUINT16Input();
         
   // Get the upper range.
   (void) printf("\r\nPlease enter the upper range in microseconds. ");
   *upperBoundaryUs = getUINT16Input();   
}

//*****************************************************************************
// This unmitigated piece of crap will get UINT16 input from the keyboard and
// turn it into something a drill sergent would be proud of.
//
// Parameters: NONE
//
// Return: A value between 0 and 65535
//*****************************************************************************
UINT16 getUINT16Input(void) 
{
   UINT8 buffer [6];
   INT8 bufferIndex = 0;
   UINT8 carriageRet = '\r';
   UINT16 value = 0;
   
      // Read the digits into a buffer until you get a carage return.
   do
   { 
      // Fetch and echo the user input
      buffer[bufferIndex] = GetChar();
      (void)printf("%c", buffer[bufferIndex]);
      
      // If it's a digit store it.
      if(isdigit( buffer[bufferIndex]) && bufferIndex < 5) 
      {
        ++bufferIndex;
      } 
      else if (bufferIndex == 6) 
      {
        // Bugger this!!! Input is too long and we're breaking out of this loop crap. 
        buffer[bufferIndex] = carriageRet;
      }    
   }
   while( buffer[bufferIndex] != carriageRet); 
   
   // append a null character on the end of our array.
   buffer[bufferIndex] = 0;
   
   // Debug code
//   (void)printf("\n  buffer[bufferIndex] %s\n",  buffer); 
   
   value = atoi (buffer);
      
   return value;  
}

//*****************************************************************************
// This unmitigated piece of crap will test to make sure the timer is running
// on the board.  If it is not running it will print an error message and fail.
//
//
// Parameters:  None.
//
// Return: None.
//*****************************************************************************
UINT16 post_function(void){
  UINT16 timer_check_value1, timer_check_value2;   //Two values to check whether timer is running or not.
  UINT8 i;
  timer_check_value1 =  TCNT;                      //Take first value at some time.
  for(i=0;i<200;i++) {                             //Take some rest before reading second value
  }
  timer_check_value2 =  TCNT;                      //Take second value at some time
  if (timer_check_value2 == timer_check_value1) {
    (void)printf("POST failed! You buggy man.\r\n");        //POST get failed. Big reason to worry!
    return FALSE;
  }
  return TRUE;
}

//*****************************************************************************
// This unmitigated piece of crap will take the timing measurements and
// insert them into the correct histogram bucket.  The histogram range is between
// lowerBoundaryUs and upperBoundaryUs.  In addition, it will keep track of the
// lowest value for each bucket.
//
// The histogram is stored in the histogram table in the global namespace.
// The minimum values for each bucket are stored in the minimumHistogramValue table
// in the global namespace.
//
// Parameters:
//    lowerBoundaryUs  A pointer to where the lower boundary value is to be stored.
//    upperBoundaryUs  A pointer to where the upper boundary value is to be stored.
//
// Return: None.
//*****************************************************************************
void processTimerMeasurements(UINT16 lowerBoundaryUs, UINT16 upperBoundaryUs) 
{
   const UINT16 maxUnsignedValue = 65535;                
   int i = 0;
   int histogramIndex = 0;
     
   // calculate out the size of each bucket.
   int quotent = (upperBoundaryUs - lowerBoundaryUs) / numberOfBuckets;
   
   // calculate the pulse intervals and store them.
   for (i = 0; i < MAXINPUTVALUES - 1; ++i) 
   {
     if (  timerValuesUs [i] < timerValuesUs [i + 1]) 
     {
        pulseIntervalsUs[i] = timerValuesUs [i + 1] - timerValuesUs [i];
     } 
     else 
     {
         // don't mess with this function or you'll end up with a result greater than 65535.
         // which means you'll get wrap around.
         pulseIntervalsUs[i] = maxUnsignedValue - timerValuesUs [i] + timerValuesUs [i + 1];   
     }
     
     // This is debug code 
     //(void)printf("pulseIntervalsUs[%d]  %u\r\n", i, pulseIntervalsUs[i]);
   }
   
    // Construct the histogram and update the lowest value for each histogram bucket.
   for (i = 0; i < MAXINPUTVALUES - 1; ++i) 
   {
      if(pulseIntervalsUs[i] < lowerBoundaryUs)
      {
        (void)printf("Error: pulseIntervalsUs[%d] %u is below the lower range\r\n", i, pulseIntervalsUs[i]);
      }
      else if (pulseIntervalsUs[i] > upperBoundaryUs )
      {
         (void)printf("Error:pulseIntervalsUs[%d] %u is above the upper range\r\n", i, pulseIntervalsUs[i]);
      } 
      else 
      {
         // the value falls in the area of interest so add it to the histogram
         
         // calculate the index for the histogram
         histogramIndex = ((pulseIntervalsUs[i] - lowerBoundaryUs) / quotent);
         
         //(void)printf("histogramIndex %d = %u\r\n", i, histogramIndex);
         
         // Check to see if we need to update the lowest value for that bucket 
         if (histogram[histogramIndex] == 0) 
         {
              // This bucket is empty.  Just add the value to it.
              minimumHistogramValueUs[histogramIndex] = pulseIntervalsUs[i];
              
              // This is debug code  
              // (void)printf("Empty Bucket %d minimumHistogramValue[%d]  %u\r\n", i, histogramIndex, minimumHistogramValue[i]);
         } 
         else if (pulseIntervalsUs[i] < minimumHistogramValueUs[histogramIndex]) 
         {
              // we have a new lowest value for that bucket.
               minimumHistogramValueUs[histogramIndex] = pulseIntervalsUs[i]; 
                   
               // This is debug code  
               // (void)printf("Changed value is %d minimumHistogramValue[%d]  %u\r\n", i, histogramIndex, minimumHistogramValue[i]);
         }
         
         // increment the histogram bucket;
         ++histogram[histogramIndex]; 
      }
   }
}

 

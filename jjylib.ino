/*
 JJY radio parsing
 
 ISR fills a buffer with ON/OFF pulse pairs.
 A normal running loop function validates and turns them into a string of characters
 A separate parser function parses the string and fills in the time data structure
 
 */

// Turn this on to use fake input data just to test parsing etc.
#define FAKE_INPUT_DATAxxx

// This logs metrics like pulses-per-second, extracted bits from
// the time signal, etc. It also causes timing problems which 
// cause things to break once we have around 30 bits of data accumulated
#define LOG_SAMPLESxxx

int PIN_LED       = 9;   // Signal coming through indicator
int PIN_BUTTON    = 8;
int PIN_INPUT     = 3;

// Raw input buffer filled by ISR
// 100 samples at 4 bytes per is around 0.5K of 2K RAM used
#define SAMPLE_MAX 100
#ifdef FAKE_INPUT_DATA
unsigned long inputTimestamps[SAMPLE_MAX] = 
{
302, 860, 1311, 2135, 2337, 2818, 3300, 4114, 4362, 5116,
5306, 6097, 6298, 7128, 7329, 8121, 8308, 9111, 9329, 9539,
10317, 10509, 11289, 12140, 12329, 13130, 13308, 13864, 14319,
15144, 15320, 16103, 16309, 17127, 17336, 17807, 18310, 19108,
19317, 19537, 20304, 21117, 21298, 22118, 22330, 23130, 23317,
23854, 24291, 25156, 25336, 26108, 26308, 27101, 27333, 27803,
28304, 29111, 29309, 29532, 30328, 31130, 31313, 32112, 32313,
33139, 33348, 34120, 34304, 35111, 35322, 36144, 36331, 37116,
37301, 38109, 38324, 39124, 39315, 39497, 40305, 41132, 41339,
42133, 42309, 42830, 43310, 44138, 44312, 45109, 45312, 46120,
46332, 47150, 47313
};
int nextInputIndex = 93;
boolean errorState = false;
int errorCode = 0;
#else
volatile unsigned long inputTimestamps[SAMPLE_MAX];
volatile int nextInputIndex = 0;

#ifdef LOG_SAMPLES
volatile byte loggingDurations[1000] = {0};
volatile int nextLoggingDuration = 0;
#endif

volatile boolean inputPrimed = false;
boolean ledState = false;
volatile boolean errorState = false;
volatile int errorCode = 0;
boolean pinState = true;
unsigned long lastInputTime = 0;
#endif

// Count of pulses for each of the last 4 seconds
#define PULSE_HISTORY_SIZE 4
volatile int pulseHistory[PULSE_HISTORY_SIZE];
volatile int currentPulseBucket = -1;
volatile int pulseAccumulator = 0;

// Parsed data string. The most we need is 2 minutes of data to get a full minute string
#define PARSED_DATA_MAX 120
char parsedData[PARSED_DATA_MAX+1] = {0};

// Forward decl of our interrupt handler
void dataISR(void);

// Gets the number of bytes free for stack/locals or heap allocations. Newer versions of
// the IDE display this at compile time but this gives us a sanity check if our input
// buffers are too big for the board we're using, etc.
int freeRam()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

// How many samples in last 4 seconds? If this is 8 or close to it then we are likely
// getting good data
int samplesInLast4Seconds()
{
  int result = 0;
  
  for (int i=0; i<PULSE_HISTORY_SIZE; ++i)
  {
    result += pulseHistory[i];
  }

  return result;
}


// the setup routine runs once when you press reset:
void setup()  
{ 
  Serial.begin(57600);
  
  // LED pin is an output
  pinMode(PIN_LED, OUTPUT);
  
  // Button is an input. Use pullup
  pinMode(PIN_BUTTON, INPUT);
  digitalWrite(PIN_BUTTON, HIGH);
  
  // Data input pin
  pinMode(PIN_INPUT, INPUT);
 
  // LED off to start
  digitalWrite(PIN_LED, LOW);
  
#ifndef FAKE_INPUT_DATA
  // Clear all samples
  for (int i=0; i<SAMPLE_MAX; ++i)
  {
    inputTimestamps[i] = 0;
  }
#endif
  
  // Init pulse history buffers. We seed them with crazy high
  // numbers so we don't detect a good state until it actually
  // occurs.
  for (int i=0; i<PULSE_HISTORY_SIZE; ++i)
  {
    pulseHistory[i] = 100; 
  }
    
  Serial.println("\n\nStarting...\n");
  Serial.print("FREE SRAM = "); Serial.println(freeRam());

#ifdef FAKE_INPUT_DATA
  Serial.println("\n\nFAKE DATA. No Intterupts\n");
#else
  Serial.println("\n\nAttaching interrupt...\n");
  attachInterrupt(1, dataISR, CHANGE);
  Serial.println("Done\n");
#endif
} 

#ifndef FAKE_INPUT_DATA
unsigned long lastDataWrite = 0;
void dataISR(void)
{
   // We can get called multiple times per pin change, so this debounces it
   boolean localPinState = digitalRead(PIN_INPUT) == HIGH ? true : false;
   if (pinState == localPinState)
   {
     // Called spuriously. Just exit
     return;
   }
   pinState = localPinState;
  
   // We store pairs of pulses starting with LOW. The decoder IC is active low so a low
   // pulse indicates the beginning of a second.
   if (!inputPrimed)
   {
     if (pinState)
     {
       // Wait for low pulse to begin
       return;
     }
     inputPrimed = true;
   }
      
   // Pulse bucket is millis divided by 1024 and then take lower 2 bits
   // This is why the number of buckets is a power-of-2, so we can avoid
   // doing a slow division operation in the ISR
   unsigned long ms = millis();
   int pulseBucket = (ms >> 10) & 3;
   
   // If we just switched to a new pulse bucket, then store the previous
   // count and clear accumulator. 
   if (pulseBucket != currentPulseBucket)
   {
     pulseHistory[currentPulseBucket] = pulseAccumulator;
     pulseAccumulator = 0;
     currentPulseBucket = pulseBucket;
   }
   ++pulseAccumulator;
   
   // If it's been a long time since we've been called then clear out
   // the old pulse buckets. Because that means we got zero pulses during
   // those seconds. This is important early on when we might only be getting
   // data every other second or so. It zeroes out the bogus old counts.
   unsigned long expired = (ms - lastInputTime) >> 10;
   if (expired > 3)
   {
     expired = 3;
   }
   while (expired > 0)
   {
     pulseHistory[ (currentPulseBucket + 4 - expired) & 3 ] = 0;
     expired--;
   }
   lastInputTime = ms;
     
   // Sanity check to make sure we haven't gotten out of LOW/HIGH sync. This
   // is more about validating code changes. We don't expect this to happen
   // at runtime but if it does it means there's a code error or some constraint
   // has been violated, so this doesn't bother trying to fix the issue.
   if (nextInputIndex & 1)
   {
     if (!pinState)
     {
       errorCode = 1;
       errorState = true;
     }
   }
   else
   {
     if (pinState)
     {
       errorCode = 2;
       errorState = true;
     }     
   }
   
   // Store timestamp
   inputTimestamps[nextInputIndex] = ms;
   if (++nextInputIndex == SAMPLE_MAX)
   {
     // Overran our pulse buffers. Just keep storing in the last pair slot and
     // toss away previous stuff. In normal practice this only ever happens if
     // the receiver IC is way off on AGC tuning and in those cases we can get
     // 10,000 or more pulses per second. It's all noise so we don't need to
     // store it. In normal operation with the data being parsed ever few seconds
     // it's rare for this to ge above 10 or so.
     nextInputIndex = nextInputIndex - (localPinState ? 2 : 1);
     inputPrimed = false;
   }
   
#ifdef LOG_SAMPLES
   // And finally store logging duration. We store the time delta between
   // pulses as ms/16 which gives us a range of [0..4095] ms with 16ms precision
   // If we get too many pulses in a second, then stop writing data
   if (pulseAccumulator < 20)
   {
     loggingDurations[nextLoggingDuration++] = (ms-lastDataWrite) >> 4;
   }
   else if (pulseAccumulator == 20)
   {
     // If this is the 20th duration being written during this second, then
     // write a sentinel meaning noise
     loggingDurations[nextLoggingDuration++] = 255;
   }
#endif

   lastDataWrite = ms;
}
#endif

// Called by decoder when it has consumed some amount of input data. I couldn't
// recall if memmove or memcpy was the correct one to use, so I just wrote my own
void consumeInputData(int sampleCount)
{
  noInterrupts();
  int remainder = nextInputIndex - sampleCount;
  if (remainder >= 0)
  {
    for (int i=0; i<remainder; ++i)
    {
      inputTimestamps[i] = inputTimestamps[sampleCount+i]; 
    }
    nextInputIndex = remainder; 
  }
  interrupts(); 
}

#ifdef LOG_SAMPLES
// Called by decoder when it has consumed some amount of input data
void consumeLoggingData(int sampleCount)
{
  noInterrupts();
  int remainder = nextLoggingDuration - sampleCount;
  if (remainder >= 0)
  {
    for (int i=0; i<remainder; ++i)
    {
      loggingDurations[i] = loggingDurations[sampleCount+i]; 
    }
    nextLoggingDuration = remainder; 
  }
  interrupts(); 
}
#endif

// Just tosses out all previously parsed data
void ResetDecoder()
{
  parsedData[0] = 0; 
}

// Second stage of parsing. This converts pulse time-stamps to a string of bits
// inside the parsedData variable
void DecodeInput()
{
  // Grab the current number of samples. ISR always increments this number
  // and always does so after writing the timestamp. So we can read this
  // many samples safely.
  int sampleCount = nextInputIndex;
  int sampleIndex = 0;
  
  // We can only deal with ON/OFF pairs which means 3 timestamps needed.
  // So process data until there's less than 3 samples left
  while ((sampleCount-sampleIndex) >= 3)
  {
    int on   = inputTimestamps[sampleIndex+1] - inputTimestamps[sampleIndex];
    int off  = inputTimestamps[sampleIndex+2] - inputTimestamps[sampleIndex+1];
    int both = on + off;
    
    // Validate pulse width
    if (abs(both-1000) > 100)
    {
      // Error -- invalid pulse width. Attempt to reconstruct signal from two
      // pule pairs
      if ((sampleCount-sampleIndex) >= 5)
      {
         // OK. Got two pulse pairs. Grab the second pair's data
        int on2   = inputTimestamps[sampleIndex+3] - inputTimestamps[sampleIndex+2]; 
        int off2  = inputTimestamps[sampleIndex+4] - inputTimestamps[sampleIndex+3];
        int both2 = on2 + off2;
       
        // Do the two pulse pairs add up to a single pulse?
        if (abs(both+both2-1000) < 100)
        {
          // Yes! Make a 4 bit mask indicating which of the durations is larger than 100ms. Typical
          // errors that can be reconstructed consist of a sing spurious <100ms pulse
          int errorCase = 0;
          
          if (on > 100)   errorCase |= 8;
          if (off > 100)  errorCase |= 4;
          if (on2 > 100)  errorCase |= 2;
          if (off2 > 100) errorCase |= 1;
          
          boolean reconstructed = false;
          switch (errorCase)
          {
            case 0:    // All large? Don't know what to do with this
              break;            
            
            case 1:    // Spurious + big off
            case 3:    // Spurious + big on and big off means just extend the on duration
              on  = both + on2;
              off = off2;
              reconstructed = true;
              break;
            
            // These are all cases we don't know what to do with
            case 2:    // Spurious + Big On followed by tiny off never gives anything useful
            case 4:    // This one is pure nonsense
            case 5:    // Two big offs with small ons can't give us anything usable
            case 6:    // Two big on/off pair but reversed is useless also
            case 7:    // What could this possibly be?
            case 10:   // Makes my head hurt
            case 14:
            case 15:
              break;
              
            case 8:    // Big initial on followed by noise. Could be a zero bit if the last 3 sum close to 200
              off = off + both2;
              reconstructed = true;
              break;
              
            case 9:    // Big first on, big last off. Just split the difference
              on = both;
              off = both2;
              reconstructed = true;
              break;
              
            case 11:
              on = both + on2;
              off = off2;
              reconstructed = true;
              break;
             
            case 12:
            case 13:
              off = off + both2;
              reconstructed = true;
              break;            
          }
          
          if (reconstructed)
          {
            // We actually used 4 timestamps for this, so increment by two so the normal exit will add the last 2
            sampleIndex += 2;
            goto validPulse;
          }
          
          // Looked promising, but we failed to reconstruct. So just reset the decode and try the second half of the 
          // pair against it's neighbor
        } 
      }
      ResetDecoder();
    }
    else
    {
validPulse: 
      char thisBit = 0;
      
      // Overall pulse width is good. Now check bit type.
      if (abs(on-200) < 50)
      {
        // This is a Marker framing bit
        thisBit = '^'; 
      } 
      else if (abs(on-500) < 100)
      {
        // This is a 1 bit
        thisBit = '1';
      }
      else if (abs(on-800) < 100)
      {
        // This is a 0 bit
        thisBit = '0';
      }
      
      // If we were able to parse the pulse data and extract a bit, then append it
      // onto the end of the data string
      if (thisBit)
      {
        int dataLen = strlen(parsedData);
        
        if (dataLen < PARSED_DATA_MAX)
        {
          parsedData[dataLen] = thisBit;
          parsedData[dataLen+1] = 0;
        }
      }
      else
      {
        // This hurts. We may have already accumulated some data but we gotta throw
        // it all away and start over.
        ResetDecoder();
      }
    }
      
    sampleIndex += 2;    
  }
    
  // All done with that data
  consumeInputData(sampleIndex);
}

int lastlogSecond = 0;
void logdata()
{
    int thissec = millis() >> 10;
    if (thissec != lastlogSecond)
    {
#ifdef LOG_SAMPLES
      Serial.print("s4s:");
      Serial.print(samplesInLast4Seconds());
      Serial.print(",");
      Serial.print(nextInputIndex);
      Serial.print(",");
      if (strlen(parsedData) > 0)
      {
        Serial.print(parsedData);
      }
      else
      {
        Serial.print("-");
      }
      
      // write logging durations
      int logDurLen = nextLoggingDuration;
      Serial.print(",");
      Serial.print(logDurLen);
      for (int i=0; i<logDurLen; ++i)
      {
        Serial.print(",");
        Serial.print(loggingDurations[i] << 4);
      }
      Serial.println();
      consumeLoggingData(logDurLen);
#endif

      if (errorState)
      {
       Serial.println("\n###ERROR###");
      }
      lastlogSecond = thissec;
    }
}

// Reads a bit from the parsed data string
int GetBit(const char* pStr, int iBit)
{
  if (iBit < 0 || iBit >= strlen(pStr))
  {
    return 0;
  }

  return pStr[iBit] == '1' ? 1 : 0;
}

// 3rd stage of decoding the time signal [Pulses->Bit String->Here]
// This doesn't yet bother to extract the year, month and date info
// because we're only interested in the house/minutes for now.
void ParseInput()
{
  noInterrupts();

  int i = 0;
  int len = strlen(parsedData);
  int som = -1;
  boolean timeOK = false;
  int hours, minutes;
  
  // Find start-of-minute marker
  while (i < len-1)
  {
    if (parsedData[i] == '^' && parsedData[i+1] == '^')
    {
      som = i+2;
      break;
    }    
  }
  
  // If we found it, is there enough to make sense of?
  if (som >= 0 && som+18 < len)
  {
    const char* pStr = parsedData + som;
    
     // Yes. Parse it
     minutes = GetBit(pStr, 0) * 40 +
               GetBit(pStr, 1) * 20 +
               GetBit(pStr, 2) * 10 +
               GetBit(pStr, 4) * 8  +
               GetBit(pStr, 5) * 4 +
               GetBit(pStr, 6) * 2 +
               GetBit(pStr, 7);
               
     hours = GetBit(pStr, 11) * 20 +
             GetBit(pStr, 12) * 10 +
             GetBit(pStr, 14) * 8 +
             GetBit(pStr, 15) * 4 +
             GetBit(pStr, 16) * 2 +
             GetBit(pStr, 17);
             
      timeOK = true;
  }
  interrupts();
  
  // Interrupts must be back on before we attempt any serial output
  if (timeOK)
  {
    Serial.print("Time is ");
    Serial.print(hours);
    if (minutes < 10)
    {
      Serial.print(":0");
    }
    else
    {
      Serial.print(":");
    }
    Serial.println(minutes);
  }
}

void loop()  
{
  // Write to log every second
  if ((millis() & 1023) == 0)
  {
    logdata();
  }
  
  // Decode input data every 4 seconds. Note the bit and of 4094 instead of 4095, because
  // if logging is turned on then we did some serial output at t&4095 which may have used
  // up a full millisecond and we'll never get called. So we do it a millisecond later.
  if ((millis() & 4094) == 0)
  {
    DecodeInput();
    
    // We need at least 20 bytes past the start-of-minute marker
    // to parse the current time
    if (strlen(parsedData) > 20)
    {
      ParseInput();
    }
  }
  
  // Check for button press to dump data. This is just for debugging
  if (digitalRead(PIN_BUTTON) == LOW)
  {
   int iIndex = 0;
   while (iIndex < nextInputIndex-1)
   {
     Serial.print("ON:");
     Serial.print(inputTimestamps[iIndex+1] - inputTimestamps[iIndex]);
     Serial.print("     OFF:");
     Serial.print(inputTimestamps[iIndex+2] - inputTimestamps[iIndex+1]);
     Serial.println();
     
     iIndex += 2;
   }
   
   iIndex = 0;
   while (iIndex < nextInputIndex)
   {
  
     Serial.println(inputTimestamps[iIndex]);     
     iIndex += 1;
   } 

  }

}


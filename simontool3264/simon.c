/*
Copyright (C) 2015  Brian Degnan (http://users.ece.gatech.edu/~degs)
The Georgia Institute of Technology

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
#include "simon.h"
#include "utilities.h"


/*************************************
**  Globals SIMON hardware description
*************************************/
static arbitrary_register *p_hardware_LFSR;       //the pseudo random stream
static arbitrary_register *p_hardware_togglebit;  //the toggle bit
static arbitrary_register *p_hardware_key;        //hardware key
static arbitrary_register *p_hardware_cryptotext; //the encryption register

static FILE *fp_logfile;
static FILE *fp_strobefile;

static FILE *fp_clock;       //master clock
static FILE *fp_lfsr;        //lfsr output
static FILE *fp_toggle;      //toggle bit
static FILE *fp_z;           //z sequence

static FILE *fp_key_bit;     //the output bit from the key register.
static FILE *fp_key_mux1;    //the control for key mux 1
static FILE *fp_key_mux3;    //the control for key mux 3
static FILE *fp_key_mux4;    //the control for key mux 4

static FILE *fp_crypto_bit;  //the output bit from the encryption regsiter
static FILE *fp_crypto_mux0; //the control for crypto mux 0
static FILE *fp_crypto_mux1; //the control for crypto mux 1
static FILE *fp_crypto_mux8; //the control for crypto mux 8




/*************************************
**  Global debug
*************************************/
//#define KEYTESTS
// KEYTESTS prints out the expansion for a key for the first block size plus 1
// for instance, if you want to look an encrypt/decrypt set:
// ./bin/simontool.elf -e -b 32 -k 64 -s 9669966996699669 -t 65656877
// ./bin/simontool.elf -d -b 32 -k 64 -s 3ec52Cd32cd32cd3 -t 00000000
//  the decryption argument is 16 cycles into the encryption, so you can see it back calculate the keys.

//#define MUXTESTS
// MUXTESTS prints out the MUX control on a per-cycle basis.

//#define CRYPTOTESTS
//shows the series of encryption

//output the log file format as a LaTeX table
//#define LATEXLOG



static i32 debug_reporting = 0;

/*************************************
**  Functions for SIMON
*************************************/


/*
**  simoncipherconfig* simon_create()
**  create a framework for SIMON and set default values.  These values do not mean
**  that SIMON will function, but they are sane values that will be overwritten when
**  the virtual hardware is created.
*/

simoncipherconfig* simon_create()
{
	//allocate all memory	
	simonconfigptr = calloc(1,sizeof(simoncipherconfig));  //allocate and zero the memory
	//program flow defaults
	simonconfigptr->debug = -1;  //disable debug
	simonconfigptr->debug_zvalue=-1;  //zvalue to present
	simonconfigptr->debug_writetolog=0;  //a zero value means that nothing is written.
	simonconfigptr->debug_strobes=SIMON_STROBE_OFF;
	
    //program flow defaults
	simonconfigptr->clock_number=0;  //number of times to clock the system, zero for infinite.
	simonconfigptr->clock_count=0;  //The times of times the system has been clocked.
	simonconfigptr->clock_magnitude=-6; //microseconds
	
	//simon cipher defaults
	simonconfigptr->block_bit_length=32;
	simonconfigptr->key_bit_length=64;
	simonconfigptr->status_key_length=_SIMON_ARRAY_LENGTH;
	simonconfigptr->seq_Z=0;  //this value will be selected based on the key/block
	simonconfigptr->rounds_T=0;
	//simon cipher derived values
	simonconfigptr->derived_n = (simonconfigptr->block_bit_length)/2;
	simonconfigptr->derived_m = (simonconfigptr->key_bit_length)/simonconfigptr->derived_n;
	simondataptr= calloc(1,sizeof(simondata)); 
	
	simon_set_voltage_ascii(_SIMON_DEFAULT_VOLTAGE);  //set the default voltage
	
	
	return(simonconfigptr);
}

u32 simon_destroy()
{
  free(simondataptr);
  free(simonconfigptr);
  return(0);
}
/*
**  simon_set_key_ascii(const u8 *p_keystring)
**  Set the KEY value via the -s command
**
*/
void simon_set_key_ascii(const char *p_keystring)
{
    u32 l_strlen=strnlen(p_keystring,(_SIMON_ARRAY_LENGTH-1));
	strncpy((char *)(simonconfigptr->key_ascii),p_keystring,l_strlen);
}
/*
**  simon_set_test_crypto_ascii(const u8 *p_keystring)
**  This function allows an ASCII string to be passed as a hexvalue to test
**  a round of encryption. 
*/
void simon_set_test_crypto_ascii(const char *p_cryptostring)
{
    u32 l_strlen=strnlen(p_cryptostring,(_SIMON_ARRAY_LENGTH-1));
	strncpy((char *)(simonconfigptr->test_crypto_ascii),p_cryptostring,l_strlen);
}

/*
**
*/
void simon_set_encrypt()
{
	u32 l_configuration = SIMON_ENCRYPT;
	simonconfigptr->simon_configuration = (simonconfigptr->simon_configuration)| l_configuration;

}

void simon_set_decrypt()
{
	u32 l_configuration = ~SIMON_ENCRYPT;  //logical inverse
	simonconfigptr->simon_configuration = (simonconfigptr->simon_configuration)& l_configuration;
}

/*
**  simon_set_logfile_ascii(const u8 *p_logfile)
**  Set the name of the logfile and enable output to the log
**
*/
void simon_set_logfile_ascii(const char *p_logfile)
{
	u32 l_strlen=strnlen(p_logfile,(_SIMON_ARRAY_LENGTH-1));  //get the length, or one short of the max
	simonconfigptr->debug_writetolog = 1;  //if this value is > 0, we write to the log
	strncpy((char *)(simonconfigptr->logfile_ascii),p_logfile,l_strlen);
}

/*
**  simon_set_voltage_ascii(const u8 *p_logfile)
**  Set the name of the logfile and enable output to the log
**
*/
void simon_set_voltage_ascii(const char *p_voltage)
{
	u32 l_strlen=strnlen(p_voltage,(_SIMON_ARRAY_LENGTH-1));  //get the length, or one short of the max
	strncpy((char *)(simonconfigptr->voltage_ascii),p_voltage,l_strlen);
}
/*
**  i32 simon_get_asciibit(u8 *p_ascii, u32 r_bitlocation)
**  return the bit at the index of p_ascii, or return -1 if out of bounds.
**  byte 0 is on the far left of the array, and you move to the right
**  however, bit 0 is byte 0 is the left bit, so in 0x8, 1 is bit 0
*/
i32 simon_get_asciibit(u8 *p_ascii, u32 r_bitlocation)
{
	u32 u32_len = strlen((char *)(p_ascii)) *4;  //string length times 4 should be the total bit count.
	u32 bit_location = 0;
	u32 byte_location = 0;
	u8  hexval=0;
	u8  mask=0;
	if(r_bitlocation>u32_len)
	{
		return(-1);  //the bit is outside boundary for a bit request.
	}
	// now we can consider out ASCII input where we covert the byte via
	// hexchar_to_nibble, and then can return a bit.
	byte_location = r_bitlocation/4;  //this will get our byte.
	hexval= hexchar_to_nibble(p_ascii[byte_location]);
	bit_location = (3-r_bitlocation%4);  // 3 - bit location inverts order.
	//fprintf(stdout, "%i ,%i\n",byte_location,bit_location);
	mask= (1 << bit_location);  //shift to create a bit mask.
	hexval=hexval&mask;  //mask off the bit.
	if(hexval!=0)
	{	//fprintf(stdout,"1");
		return(1);
	}
	//fprintf(stdout,"0");
	return(0);
}


/*
**  simon_set_keysize(u32 p_keylength)
**  set the keysize of SIMON.  The function rounds up to nearest power of 2.
*/
u32 simon_set_keysize(u32 p_keylength)
{
  u32 u32_tmp;
  u32_tmp=util_validroundsize(p_keylength);  //get the nearest "power" of the number
  //printf("^^ %i=util_validroundsize(%i)\n",u32_tmp,p_keylength);
  simonconfigptr->key_bit_length=u32_tmp; 
  return(p_keylength); 
}
u32 simon_get_keysize()
{
return(simonconfigptr->key_bit_length);
}

/*
**  simon_set_blocksize(u32 p_keylength)
**  set the blocksize of SIMON.  The function rounds up to nearest power of 2.
*/
u32 simon_set_blocksize(u32 p_blocklength)
{
  u32 u32_tmp;
  u32_tmp=util_validroundsize(p_blocklength);  //get the nearest "power" of the number
  simonconfigptr->block_bit_length=u32_tmp; 
  return(p_blocklength); 
}
/*
**  simon_get_blocksize(u32 p_keylength)
**  get the blocksize of SIMON. 
*/
u32 simon_get_blocksize()
{
  return(simonconfigptr->block_bit_length); 
}
/*
**  simon_set_clocknumber(u32 p_clock)
**  set the amount of times to "clock" the SIMON system
*/
void simon_set_clocknumber(u32 p_clock)
{
  simonconfigptr->clock_number=p_clock; 
}
void simon_debug_set_z(i32 p_zindex)
{
	if(p_zindex>=0 && p_zindex<5)
	{   simonconfigptr->debug_zvalue=p_zindex;  }
	else{ simonconfigptr->debug_zvalue = 0;}
}
/*
**  simon_debug_setdebug(i32 p_debuglevel)
**  set the debug level, where 0 is no debugging
*/
u32 simon_debug_setdebug(i32 p_debuglevel)
{
	simonconfigptr->debug=p_debuglevel;  
	return(simonconfigptr->debug);
}

/*
**  simon_debug_setstrobes(u32 p_debuglevel)
**  set the strobe level
*/

void simon_debug_setstrobes(u32 p_debuglevel)
{
    simonconfigptr->debug_strobes=p_debuglevel; 
}

/*
**  simon_debug_printstructure(FILE *p_stream)
**  print the simon configuration out.
*/
void simon_debug_printstructure(FILE *p_stream)
{
	u32 u32_tmp=0;
	i32 i32_tmp=0;
	fprintf(p_stream,"SIMON Configuration\n");
	i32_tmp = simonconfigptr->debug;
	fprintf(p_stream,"  debug  =0x%08x (%i)\n",i32_tmp,i32_tmp);
	i32_tmp = simonconfigptr->debug_zvalue;
	fprintf(p_stream,"  debug_zvalue  =0x%08x (%i)\n",i32_tmp,i32_tmp);
	i32_tmp = simonconfigptr->debug_writetolog;
	fprintf(p_stream,"  debug_zvalue  =0x%08x (%i)\n",i32_tmp,i32_tmp);
	u32_tmp = simonconfigptr->clock_number;
	fprintf(p_stream,"  clock_number  =0x%08x (%i)\n",u32_tmp,u32_tmp);
	
	u32_tmp = simonconfigptr->block_bit_length;
	fprintf(p_stream,"  block_bit_length  =0x%08x (%i)\n",u32_tmp,u32_tmp);
	u32_tmp = simonconfigptr->key_bit_length;
	fprintf(p_stream,"  key_bit_length    =0x%08x (%i)\n",u32_tmp,u32_tmp);
	u32_tmp = simonconfigptr->status_key_length;
	fprintf(p_stream,"  status_key_length =0x%08x (%i)\n",u32_tmp,u32_tmp);
	u32_tmp = simonconfigptr->seq_Z;
	fprintf(p_stream,"  seq_Z =0x%08x (%i)\n",u32_tmp,u32_tmp);
	u32_tmp = simonconfigptr->rounds_T;
	fprintf(p_stream,"  rounds_T =0x%08x (%i)\n",u32_tmp,u32_tmp);
	
	fprintf(p_stream,"  key[%lu]: %s \n",strlen((const char *)(simonconfigptr->key_ascii)),simonconfigptr->key_ascii);
	fprintf(p_stream,"  text[%lu]: %s \n",strlen((const char *)(simonconfigptr->test_crypto_ascii)),simonconfigptr->test_crypto_ascii);
	fprintf(p_stream,"  logfile[%lu]: %s \n",strlen((const char *)(simonconfigptr->logfile_ascii)),simonconfigptr->logfile_ascii);
	
}
void simon_debug_simondata(FILE *p_stream)
{
   i32 l_tmpi32=0;
   i32 l_counter=0;
   fprintf(p_stream,"SIMON Data\n");
   l_tmpi32 = simondataptr->simon_bytecount;
   fprintf(p_stream,"  simon_bytecount = 0x%08x (%i)\n",l_tmpi32,l_tmpi32);
   fprintf(p_stream,"   data_in: ");
   for(l_counter=0;l_counter<(simondataptr->simon_bytecount);l_counter++)
   {
   		fprintf(p_stream,"%02x",simondataptr->data_in[l_counter]);
   }
   fprintf(p_stream,"\n");
   fprintf(p_stream,"  data_out: ");
   for(l_counter=0;l_counter<(simondataptr->simon_bytecount);l_counter++)
   {
   		fprintf(p_stream,"%02x",simondataptr->data_out[l_counter]);
   }
   fprintf(p_stream,"\n");
   
}

void simon_debug_hardware_dumpkey()
{	
	//arbreg_debug_dumpbits(p_hardware_key);
	arbreg_debug_dumphex(p_hardware_key);
}


/*
**  simon_init_lfsr()
**  enc: This needs to set the bit correctly for shifting left to right
**  or right to left.  The document specifies right shifting, but
**  I'm shifting it left, so 10000, where 1 is bit 4.
**
**  decrypt:  The correct state needs to be set when you start. :/
**  This varies between version of decryption rounds.  
*/
void simon_init_lfsr(u8 r_initialvalue,u8 r_mode)
{
	arbreg_setbyte(p_hardware_LFSR,0,r_initialvalue);  //assuming the LFSR is 5 bits, so the index is always 0
	if(r_mode==0)
	{   //01 initial setting
	   arbreg_setbit(p_hardware_togglebit, 0,1);  //set the initial state of 0,1, will shift left.
    }else
    {  //10 as initial code
       arbreg_setbit(p_hardware_togglebit, 1,1);
    }
}



/*
**  simon_clock_lfsr(arbitrary_register *p_arbreg)
**  simon_clock_lfsr implements the "step" which is equivalent to a logical
**  clock of the shift register.  There are only 3 actual shift register
**  implementations; however, there are 5 total Z.  The "toggle" bit is added later
**  because it would just be handled by an XOR in the hardware.
*/

i32 simon_hardware_clock_lfsr_encrypt(arbitrary_register *p_arbreg)
{
	u8 bit0=0;  //right most bit
	u8 bit1=0;
	u8 bit2=0;
//	u8 bit3=0;
	u8 bit4=0;
	u8 opres0=0;
	u8 opres1=0;
	
	//  the logic is different depending on the the value of the Z sequence;
	//  however, this block is ONLY the LSFR, to which there are 3 possible
	//  values.
	switch(simonconfigptr->seq_Z)
	{
		case 0:  //Z0:  c+e  a    b    c    d+e
		  bit4=arbreg_getbit(p_arbreg, 4);  	
		  bit2=(arbreg_getbit(p_arbreg, 2));
		  opres0= bit2^bit4;
		  bit0=arbreg_getbit(p_arbreg, 4); 	
		  bit1=(arbreg_getbit(p_arbreg, 3));
		  opres1= bit0^bit1;
		  arbreg_rol(p_arbreg,1); //roll left, but then insert right bit.
		  arbreg_setbit(p_arbreg, 0,opres0);
		  arbreg_setbit(p_arbreg, 4,opres1);		
		  break;

		
		default:
		printf("Incorrect Z! \n");
		exit(0);
	      break;
	}
	return(0);
}

/*
**  void simon_hardwarecreate()
**  Create the initial, sane, conditions for the simon simulator
**  Also, open and setup files.
*/

void simon_hardwarecreate()
{
	//--before we create the virtual hardware, we need to be sure to know the bitwidths
	//--and therefore we do a sanity check on the structure.
	u32 u32_blocklength=simonconfigptr->block_bit_length;
	u32 u32_keylength=simonconfigptr->key_bit_length;		
	u32 u32_T =0;
	u32 u32_Z =0;

    //--log and file configuration
	if(simonconfigptr->debug_writetolog > 0)
	{
	   fp_logfile=fopen((const char *)(simonconfigptr->logfile_ascii),"w+");
	}


	
	if(simonconfigptr->debug_strobes && SIMON_STROBE_ON)
	{
	  fp_strobefile=fopen("strobes.pwl","w+");
	  fp_clock=fopen("clock.pwl","w+");
	  fp_lfsr=fopen("lfsr.pwl","w+");
	  fp_z=fopen("z.pwl","w+");
	  fp_toggle=fopen("toggle.pwl","w+");
	  
	  fp_key_bit=fopen("key_bit.pwl","w+");
	  fp_key_mux1=fopen("key_mux1.pwl","w+");
	  fp_key_mux3=fopen("key_mux3.pwl","w+");
	  fp_key_mux4=fopen("key_mux4.pwl","w+");
	  
	  fp_crypto_bit=fopen("crypto_bit.pwl","w+");
	  fp_crypto_mux0=fopen("crypto_mux0.pwl","w+");
	  fp_crypto_mux1=fopen("crypto_mux1.pwl","w+");
	  fp_crypto_mux8=fopen("crypto_mux8.pwl","w+");	  
	}
	
	
	//the values for this switch are in Table 3.1 of the SIMON document.
    u32_keylength=64;
    u32_T=32;
    u32_Z=0;
	

	
	simonconfigptr->block_bit_length=u32_blocklength;
	simonconfigptr->key_bit_length=u32_keylength;
	simonconfigptr->rounds_T=u32_T;
	simonconfigptr->seq_Z=u32_Z;
	// derived values 
	simonconfigptr->derived_n = (simonconfigptr->block_bit_length)/2;  //calculate the n word size 
	simonconfigptr->derived_m = (simonconfigptr->key_bit_length)/(simonconfigptr->derived_n);// the number of key words, m	
	//--create all of the "hardware" requirements
	p_hardware_LFSR = arbreg_create(5);  //hardware representation of LFSR
	//  --simon_init_lfsr(p_hardware_LFSR);    //LFSR initial condition
	//  the lfsr state is uniform for encryption, but not decryption.
	//  The setup was moved to the beginning of the encryption or decryption   
	p_hardware_togglebit = arbreg_create(2);  //hardware representation of a 2-bit toggle register
	p_hardware_key = arbreg_create(u32_keylength);  //set the key length
	p_hardware_cryptotext=arbreg_create(simonconfigptr->block_bit_length);  //crypto text length
	
	
}

/*
**  simon_hardwaredestroy()
**  release memory for simon and close files
*/

void simon_hardwaredestroy()
{

    arbreg_destory(p_hardware_cryptotext);
	arbreg_destory(p_hardware_key);
	arbreg_destory(p_hardware_togglebit);
	arbreg_destory(p_hardware_LFSR);

    if(simonconfigptr->debug_strobes && SIMON_STROBE_ON)
    {
    
      fclose(fp_key_bit);
	  fclose(fp_key_mux1);
	  fclose(fp_key_mux3);
	  fclose(fp_key_mux4);
	  
	  fclose(fp_crypto_bit);
	  fclose(fp_crypto_mux0);
	  fclose(fp_crypto_mux1);
	  fclose(fp_crypto_mux8);	 
    
	  fclose(fp_toggle);
	  fclose(fp_z);
	  fclose(fp_lfsr);
	  fclose(fp_clock);
	  fclose(fp_strobefile);
    }
	if(simonconfigptr->debug_writetolog > 0)
	{
	   fclose(fp_logfile);
	}
}


/*
**  simon_hardware_getLFSR()
**  This function returns the state of the LFSR, which is the MSB of bit4.
**  The LFSR is incremented separately.
*/
u32 simon_hardware_getLFSR()
{
	return(arbreg_getbit(p_hardware_LFSR, 4));
}

u32 simon_hardware_gettoggle()
{	//the highest bit is the current bit, t,  used for toggle
	return(arbreg_getbit(p_hardware_togglebit, 1));
}

/*
**  simon_hardware_clockZ()
**  There are two parts to getting Z, and it depends on the state of the
**  LFSR and the toggle clock.
*/

u32 simon_hardware_getZ()
{
	u8 l_togglebit = simon_hardware_gettoggle();
	u8 l_lfsr = simon_hardware_getLFSR();
	u8 l_z = l_lfsr;
	return(l_z);
}

/*
**  simon_hardware_getK()
**  Return the K bit.  This needs to be done before the system is "clocked"
*/
u32 simon_hardware_getK()
{
     return(arbreg_getbit(p_hardware_key,0));
	//return(simonconfigptr->generated_key_bit);

}

/*
**
**
*/
void simon_pwl(FILE *fp, u32 r_line)
{
	  r_line = r_line &0x01;  //255 vs 254 check.
	  if(r_line==0)
	  {
		  fprintf(fp,"%i.0e%i 0\n",simonconfigptr->clock_count,simonconfigptr->clock_magnitude);
		  fprintf(fp,"%i.99e%i 0\n",simonconfigptr->clock_count,simonconfigptr->clock_magnitude);
	  }else
	  {
		  fprintf(fp,"%i.0e%i %s\n",simonconfigptr->clock_count,simonconfigptr->clock_magnitude,simonconfigptr->voltage_ascii);
		  fprintf(fp,"%i.99e%i %s\n",simonconfigptr->clock_count,simonconfigptr->clock_magnitude,simonconfigptr->voltage_ascii);
	  }
}


/*
**  u32 simon_hardware_inccounter()
**  Increment the hardware counter, and return the new value
**
*/
u32 simon_hardware_counter_inc()
{
	u32 u32_tmp= simonconfigptr->clock_count;
	u32_tmp= u32_tmp+1;
	simonconfigptr->clock_count=u32_tmp;
	return(u32_tmp);
}

void simon_hardware_counter_reset()
{
	simonconfigptr->clock_count=0;
}

/*
**  i32 simon_hardware_addblockbyte(u8 r_blockbyte)
**  Based on the number of bytes that the SIMON implementation is using for a 
**  chunk, you keep loading bytes until you have filed the chunk size.  This allows
**  for stream encoding as well as the test encoding.  The function returns the number
**  of remaining bytes to load.
**
**  Some assumptions are made, such as the simondataptr->simon_bytecount is zero when
**  we start this. You write in a value, and then count down as you read out the 
**  result.
*/
i32 simon_hardware_blockaddbyte(u8 r_blockbyte)
{
	u32 l_index = simondataptr->simon_bytecount;  //the index of the current byte
    simondataptr->data_in[l_index]=r_blockbyte;
    l_index = l_index +1;
    simondataptr->simon_bytecount = l_index;
    return(l_index);
}

u8 simon_hardware_getdataoutatbyte(u8 l_index)
{
	return(simondataptr->data_out[l_index]);
}
//pull data out of the data_out array
i32 simon_hardware_blocksubtractbyte()
{
	u32 l_index = simondataptr->simon_bytecount;  //the index of the current byte
    l_index = l_index - 1;
    u8 r_blockbyte=simondataptr->data_out[l_index];    
    simondataptr->simon_bytecount = l_index;
    return(r_blockbyte);
}


/*
**  simon_hardware_blocksetindex(u8 l_index)
**  this allows you to reset the pointer to z by
**  simon_hardware_blocksetindex(0);
*/
void simon_hardware_blocksetindex(u8 l_index)
{
   simondataptr->simon_bytecount = l_index;
}
/*
**
**  This should start with a verified ASCII string.
**  Copy byte by byte into the block
*/

i32 simon_hardware_loadblockfromASCII(u8 *p_asciistring)
{
   u32 l_counter;
   u32 l_tmpu32 =strlen((const char *)(p_asciistring));
   u8  l_hexvalue = 0;
   u8  l_hexhigh=0;
   u8  l_hexlow =0; 
   i32 l_lastindex=0;
   for(l_counter = 0; l_counter<(l_tmpu32);)  
   {	
      l_hexhigh= hexchar_to_nibble(p_asciistring[l_counter]);  //get the byte as ascii
      if((l_counter+1)<l_tmpu32) //check to not overrun on a non-even string
      {
      	l_hexlow= hexchar_to_nibble(p_asciistring[l_counter+1]);
      }else
      { l_hexlow=0; }
      l_hexvalue= (l_hexhigh<<4)| l_hexlow; //assemble the compete byte.
      l_lastindex=simon_hardware_blockaddbyte(l_hexvalue);
      l_counter=l_counter+2;
   }
   return(l_lastindex);
}

void simon_hardware_blockreset()
{
	simondataptr->simon_bytecount = 0;  //reset how many bytes are in the block
}

/*
**  void simon_hardware_test_loadcrypto()
**  Supports pass the "test" value to the crypto engine.  This is part of moving from
**  the legacy architecture.
*/

void simon_hardware_test_loadcrypto()
{
  simon_hardware_loadblockfromASCII(simonconfigptr->test_crypto_ascii);
  
}

void simon_hardware_test_savecrypto()
{

}

void simon_hardware_test_printcrypto()
{
   i32 l_counter=0;
   for(l_counter=0;l_counter<(simondataptr->simon_bytecount);l_counter++)
   {
   		fprintf(stdout,"%02x",simondataptr->data_out[l_counter]);
   }
}

/*
**  void simon_hardware_loadregfromASCII()
**  The ASCII key is copied into the binary register.  The following arguments:
**  arbitrary_register *p_arbreg, which is the register
**  u8 *p_asciistring, which is the ASCII string
**  u32 r_asciilen, which is the bit length to FILL the register.
**  The r_asciilen value will let you fill the register with 0 as padding
**  beyond the length of the /0 terminated p_asciistring
*/
void simon_hardware_loadregfromASCII(arbitrary_register *p_arbreg,u8 *p_asciistring,u32 r_asciilen)
{

   //fprintf(stdout,"total key length: %i\n",u32_total_keylength);
   u32 u32_tmp=0;
   u32 counter=0;
   u8  bitval =0;

	u32_tmp=strlen((const char *)(p_asciistring));  //the length of the string. 
	u32_tmp = u32_tmp * 4;  //convert to bit length
	if(r_asciilen < u32_tmp)       //truncate the string if necessary.                       
	{ u32_tmp = r_asciilen;}                     
	//The nibbles are in a string of text, and I load things bit-wise.  
	for(counter = 0; counter<(u32_tmp);counter++)  //loop and load the register with the key as binary
	{	
		bitval=simon_get_asciibit(p_asciistring,counter);
		bitval=arbreg_internal_inst_insertbitl(p_arbreg,bitval);
	}
	for(counter = (u32_tmp); counter<(r_asciilen);counter++)
	{
		if(simon_get_asciibit(p_asciistring,counter)<0)  //simon_get_key_asciibit returns -1 if out of range, but bitval is uchar
		{bitval=0;}
		else
		{   bitval=simon_get_asciibit(p_asciistring,counter);  }
		bitval=arbreg_internal_inst_insertbitl(p_arbreg,bitval);
		//fprintf(stdout,"bit count: %03i: %x\n",counter,bitval);   
	}   
}

/*
**  simon_hardware_loadblock()
**  copies the the data_in blocks into the encryption register 
*/

i32 simon_hardware_loadblock()
{
	u32 l_bytecount = simondataptr->simon_bytecount; 
	u32 l_counter = 0;
	u32 keyword_count=(simonconfigptr->block_bit_length)/8;
//	fprintf(stdout, "test: %i==%i\n", l_bytecount,keyword_count);
	if(l_bytecount==keyword_count)
	{
	  for(l_counter=0;l_counter<l_bytecount;l_counter++)
	  {	    
	    arbreg_setbyte(p_hardware_cryptotext,l_counter,simondataptr->data_in[(l_bytecount-1)-l_counter]);
	  }
	}else
	{	//different byte numbers.
	  return(-1);
	}
	return(0);

}

/*
**  simon_hardware_muxcontroldecrypt(u32 clk_count)
**  this function simulates the logic hardware this is required for the decryption
**  registers, and key generation.  In order to be consistent, the lower switch
**  values are "0" on the mux control line.
**
*/
void simon_hardware_muxcontroldecrypt(u32 clk_count)
{
    //  encryption MUX use the high bit counts, so they are offsets form the wordsize
    //  This can complicate things because we don't know which key is being used.
    u32 word_size=simonconfigptr->derived_n;  //for 32/64, it's 16 for example.
	u32 l_encmux0 = 0;  //at the MSB
	u32 l_encmux1 = 0;  //at MSB -1
	u32 l_encmux8 = 0;  //at MSB -7
    //  key mux for decryption use the lower bits
    u32 l_keymux4=0;
    u32 l_keymux3=0;
    u32 l_keymux1=0;

  /*
  **  The decryption mux is one step off of the encryption mux 
  **  key1 -> Mux1 -> key2 -> mux2 -> key3 
  **  therefore:  key3->mux2->key2
  **
  */


	if((clk_count)==(word_size-1))
	{  l_encmux0=0;
	}else
	{  l_encmux0=1;
	}
    if((clk_count)>=((word_size)-2))
    {  l_encmux1=0;
    }else
    {  l_encmux1=1;
    }if((clk_count) >=(word_size)-8)
    {  l_encmux8=0;
    }else
    {  l_encmux8=1;
    }  
      
    simonconfigptr->generated_crypto_mux0=l_encmux0;
    simonconfigptr->generated_crypto_mux1=l_encmux1;
    simonconfigptr->generated_crypto_mux8=l_encmux8;

    //--key expansion mux
    if((clk_count <= 2))
    {  l_keymux3=1;}
    else{  l_keymux3=0;}
    if((clk_count <=3))
    {  l_keymux4=1;}
    else{  l_keymux4=0;}
 
    if(clk_count ==0)
    {  l_keymux1=1;}
    else{  l_keymux1=0;}


    simonconfigptr->generated_key_mux3=l_keymux3;
    simonconfigptr->generated_key_mux4=l_keymux4;
    simonconfigptr->generated_key_mux1=l_keymux1;
    
#ifdef MUXTESTS   
    // DEBUG MUX strobes, just leave as comment
    if(simonconfigptr->clock_count < word_size)
    {
      	if(1){  //debug output
      	  if(simonconfigptr->clock_count==0){ 
      	    fprintf(stdout,"cycle   km4 km3 km1 cm8 cm1 cm0\n");
      	    }
      	  fprintf(stdout,"%02i       %i   %i   %i   %i   %i   %i  \n", simonconfigptr->clock_count,simonconfigptr->generated_key_mux4, simonconfigptr->generated_key_mux3, simonconfigptr->generated_key_mux1, simonconfigptr->generated_crypto_mux8,simonconfigptr->generated_crypto_mux1,simonconfigptr->generated_crypto_mux0);
           
        }else{  //LaTeX table creation line for the above
          fprintf(stdout," %02i & %02i& %i  & %i &  %i &  %i &  %i &  %i \\\\ \n", simonconfigptr->clock_count,(word_size-1)-simonconfigptr->clock_count,simonconfigptr->generated_key_mux4, simonconfigptr->generated_key_mux3, simonconfigptr->generated_key_mux1, simonconfigptr->generated_crypto_mux8,simonconfigptr->generated_crypto_mux1,simonconfigptr->generated_crypto_mux0);
          fprintf(stdout,"\\hline\n");
        }
    }
#endif    

    
    
}

/*
**  simon_hardware_muxcontrolencrypt(u32 clk_count)
**  this function simulates the logic hardware this is required for the encryption
**  registers, and key generation.  In order to be consistent, the lower switch
**  values are "0" on the mux control line.
**
*/
void simon_hardware_muxcontrolencrypt(u32 clk_count)
{
  
    //  encryption MUX use the lower count bits
	u32 l_encmux0 = 0;  //at the MSB
	u32 l_encmux1 = 0;  //at MSB -1
	u32 l_encmux8 = 0;  //at MSB -7
    //  key mux use the upper bits
    //  This can complicate things because we don't know which key is being used.
    u32 word_size=simonconfigptr->derived_n;  //for 32/64, it's 16 for example.
    u32 l_keymux4=0;
    u32 l_keymux3=0;
    u32 l_keymux1=0;

    //--encryption MUX
    //  when the clock is 0, select the first mux line.
	if((clk_count)==0)
	{  l_encmux0=0;
	}else
	{  l_encmux0=1;
	}
    if((clk_count)<=1)
    {  l_encmux1=0;
    }else
    {  l_encmux1=1;
    }if((clk_count) <=7)
    {  l_encmux8=0;
    }else
    {  l_encmux8=1;
    }    
    simonconfigptr->generated_crypto_mux0=l_encmux0;
    simonconfigptr->generated_crypto_mux1=l_encmux1;
    simonconfigptr->generated_crypto_mux8=l_encmux8;

    //--key expansion mux
   
    if(clk_count >= ((word_size)-3))
    {  l_keymux3=1;}
    else{  l_keymux3=0;}
    if(clk_count >=((word_size)-4))
    {  l_keymux4=1;}
    else{  l_keymux4=0;}
 
    if(clk_count ==((word_size)-1))
    {  l_keymux1=1;}
    else{  l_keymux1=0;}
   
   
    simonconfigptr->generated_key_mux3=l_keymux3;
    simonconfigptr->generated_key_mux4=l_keymux4;
    simonconfigptr->generated_key_mux1=l_keymux1;
 
   //--DEBUG STROBES, just leave this as commented
#ifdef MUXTESTS   
    if(simonconfigptr->clock_count <word_size)
    {
      	if(1){  //debug output
      	  if(simonconfigptr->clock_count==0){ 
      	    fprintf(stdout,"cycle   km4 km3 km1 cm8 cm1 cm0\n");
      	    }
      	  fprintf(stdout,"%02i       %i   %i   %i   %i   %i   %i  \n", simonconfigptr->clock_count,simonconfigptr->generated_key_mux4, simonconfigptr->generated_key_mux3, simonconfigptr->generated_key_mux1, simonconfigptr->generated_crypto_mux8,simonconfigptr->generated_crypto_mux1,simonconfigptr->generated_crypto_mux0);
           
        }else{  //LaTeX table creation line for the above
          fprintf(stdout," %02i & %02i& %i  & %i &  %i &  %i &  %i &  %i \\\\ \n", simonconfigptr->clock_count,(word_size-1)-simonconfigptr->clock_count,simonconfigptr->generated_key_mux4, simonconfigptr->generated_key_mux3, simonconfigptr->generated_key_mux1, simonconfigptr->generated_crypto_mux8,simonconfigptr->generated_crypto_mux1,simonconfigptr->generated_crypto_mux0);
          fprintf(stdout,"\\hline\n");
        }
    }
#endif       
    /*
    **  The mux sequence for the key and control for 32/64 with a 16-bit word
    **  the cycle and kmX is the key mux line with cmX the control line
	cycle   km4 km3 km1 cm8 cm1 cm0
	00       0   0   0   0   0   0  
	01       0   0   0   0   0   1  
	02       0   0   0   0   1   1  
	03       0   0   0   0   1   1  
	04       0   0   0   0   1   1  
	05       0   0   0   0   1   1  
	06       0   0   0   0   1   1  
	07       0   0   0   0   1   1  
	08       0   0   0   1   1   1  
	09       0   0   0   1   1   1  
	10       0   0   0   1   1   1  
	11       0   0   0   1   1   1  
	12       1   0   0   1   1   1  
	13       1   1   0   1   1   1  
	14       1   1   0   1   1   1  
	15       1   1   1   1   1   1  
       */
    
}

u8 simon_hardware_decryptupdate()
{
#ifdef CRYPTOTESTS  
	u32 l_debugformatting_size = (simonconfigptr->derived_n)/(simonconfigptr->derived_m * 2);
#endif
   u32 word_size=simonconfigptr->derived_n;
 //  printf("word_size: %i\n",word_size);
   u32 word_highindex=0;
   u8 bit_n1=0; u8 bit_n2=0; u8 bit_n8=0; u8 bit_feed;
   u8 bit_a18;
   u8 bit_x0=0;
   u32 l_msb = arbreg_getMSBindex(p_hardware_cryptotext);  
   word_highindex=word_size-1;
   
   /*
   **  NOTE on getting the correct K bit
   **  simon_hardware_getK pulls from the argreg structure, but this is not correct 
   **  for decryption, you need the calculated value which is the key_bit value
   */
   //u8 bit_K = simon_hardware_getK();
   u8 bit_K =simonconfigptr->generated_key_bit;
   
   /*
   ** The state of the mux are set by the function:
   ** simon_hardware_muxcontroldecrypt(CLOCK)
   ** where the clock is the bit clock
   */ 
   if(simonconfigptr->generated_crypto_mux0==0)
   {  bit_n1=arbreg_getbit(p_hardware_cryptotext,l_msb-1);
   }else
   {  bit_n1=arbreg_getbit(p_hardware_cryptotext,word_highindex-1);
   }
   if(simonconfigptr->generated_crypto_mux1==0)
   {  bit_n2=arbreg_getbit(p_hardware_cryptotext,l_msb-2);
   }else
   {  bit_n2=arbreg_getbit(p_hardware_cryptotext,word_highindex-2);
   }
   if(simonconfigptr->generated_crypto_mux8==0)
   {  bit_n8=arbreg_getbit(p_hardware_cryptotext,l_msb-8);
   }else
   {  bit_n8=arbreg_getbit(p_hardware_cryptotext,word_highindex-8);
   }   
    bit_x0=arbreg_getbit(p_hardware_cryptotext,l_msb);
    bit_a18 = bit_n1 & bit_n8;  //AND the two bits 1 and 8
    // the K that changes every completed iteration
    bit_feed = bit_K ^ bit_n2 ^ bit_a18 ^ bit_x0;
    simonconfigptr->generated_crypto_bit=bit_feed;
#ifdef CRYPTOTESTS       
	if(simonconfigptr->clock_count <=(word_size*3))
	{

	   if(simonconfigptr->clock_count%16==0)
	   {
		 fprintf(stdout,"c[%02i](%02i) ",simonconfigptr->clock_count,16-(simonconfigptr->clock_count%16));
		 arbreg_debug_dumphexmod(stdout,p_hardware_cryptotext,l_debugformatting_size);
		 fprintf(stdout,"\n");
	   }
	   fprintf(stdout,"k[%02i](%02i) ",simonconfigptr->clock_count,16-(simonconfigptr->clock_count%16));
       arbreg_debug_dumpbits(stdout,p_hardware_key,1);
	   fprintf(stdout,"c[%02i](%02i) ",simonconfigptr->clock_count,16-(simonconfigptr->clock_count%16));
	   arbreg_debug_dumpbits(stdout,p_hardware_cryptotext,1);
	   
	   fprintf(stdout,"c[%02i] ",simonconfigptr->clock_count);
	   fprintf(stdout,"b8 b2 b1 x0  k bit_feed\n");
	   fprintf(stdout,"       %i  %i  %i  %i  %i  %i\n", bit_n8, bit_n2, bit_n1, bit_x0,bit_K,bit_feed);
//	   fprintf(stdout,"c[%02i] km4 km3 km1 cm8 cm1 cm0\n",simonconfigptr->clock_count);
//	   fprintf(stdout,"       %i   %i   %i   %i   %i   %i  \n", simonconfigptr->generated_key_mux4, simonconfigptr->generated_key_mux3, simonconfigptr->generated_key_mux1, simonconfigptr->generated_crypto_mux8,simonconfigptr->generated_crypto_mux1,simonconfigptr->generated_crypto_mux0); 
	}
#endif  
   
   
    return(bit_feed);
}



/*
**  u8 simon_hardware_encrypt(u32 clk_count)
**  Encrypt the bit stream.
**  DANGER:  The key schedule starts at a full register, which works due to the ROR and
**  the fact that the things shift to toward the LSB. 
**
*/
u8 simon_hardware_encryptupdate()
{
#ifdef CRYPTOTESTS  
	u32 l_debugformatting_size = (simonconfigptr->derived_n)/(simonconfigptr->derived_m * 2);
#endif
   u32 word_size=simonconfigptr->derived_n;
 //  printf("word_size: %i\n",word_size);
   u32 word_highindex=0;
   u8 bit_n1=0; u8 bit_n2=0; u8 bit_n8=0; u8 bit_feed;
   u8 bit_a18;
   u32 l_msb = arbreg_getMSBindex(p_hardware_cryptotext);  //the MSB bit index.
   word_highindex=word_size-1;
   
   /*
   ** The state of the mux are set by the function:
   ** simon_hardware_muxcontrolencrypt(CLOCK)
   ** where the clock is the bit clock
   */ 
   if(simonconfigptr->generated_crypto_mux0==0)
   {  bit_n1=arbreg_getbit(p_hardware_cryptotext,l_msb);
   }else
   {  bit_n1=arbreg_getbit(p_hardware_cryptotext,word_highindex);
   }
   if(simonconfigptr->generated_crypto_mux1==0)
   {  bit_n2=arbreg_getbit(p_hardware_cryptotext,l_msb-1);
   }else
   {  bit_n2=arbreg_getbit(p_hardware_cryptotext,word_highindex-1);
   }
   if(simonconfigptr->generated_crypto_mux8==0)
   {  bit_n8=arbreg_getbit(p_hardware_cryptotext,l_msb-7);
   }else
   {  bit_n8=arbreg_getbit(p_hardware_cryptotext,word_highindex-7);
   }   
    bit_a18 = bit_n1 & bit_n8;  //AND the two bits 1 and 8
    // the K that changes every completed iteration
    bit_feed = simon_hardware_getK() ^ bit_n2 ^ bit_a18 ^arbreg_getbit(p_hardware_cryptotext,(0)); 
    simonconfigptr->generated_crypto_bit=bit_feed;
#ifdef CRYPTOTESTS       
	if(simonconfigptr->clock_count >=(512 - word_size*3))
	{

	   if(simonconfigptr->clock_count%16==0)
	   {
		 fprintf(stdout,"c[%02i](%02i) ",simonconfigptr->clock_count,16-(simonconfigptr->clock_count%16));
		 arbreg_debug_dumphexmod(stdout,p_hardware_cryptotext,l_debugformatting_size);
		 fprintf(stdout,"\n");
	   }
	   fprintf(stdout,"k[%02i](%02i) ",simonconfigptr->clock_count,16-(simonconfigptr->clock_count%16));
      arbreg_debug_dumpbits(stdout,p_hardware_key,1);
	   fprintf(stdout,"c[%02i](%02i) ",simonconfigptr->clock_count,16-(simonconfigptr->clock_count%16));
	   arbreg_debug_dumpbits(stdout,p_hardware_cryptotext,1);
	   
	   fprintf(stdout,"c[%02i] ",simonconfigptr->clock_count);
	   fprintf(stdout,"b8 b2 b1 C0 k bit_feed\n");
	   fprintf(stdout,"       %i  %i  %i  %i  %i  %i\n", bit_n8, bit_n2, bit_n1, arbreg_getbit(p_hardware_cryptotext,(0)),simon_hardware_getK(),bit_feed);
//	   fprintf(stdout,"c[%02i] km4 km3 km1 cm8 cm1 cm0\n",simonconfigptr->clock_count);
//	   fprintf(stdout,"       %i   %i   %i   %i   %i   %i  \n", simonconfigptr->generated_key_mux4, simonconfigptr->generated_key_mux3, simonconfigptr->generated_key_mux1, simonconfigptr->generated_crypto_mux8,simonconfigptr->generated_crypto_mux1,simonconfigptr->generated_crypto_mux0); 
	}
#endif  
    return(bit_feed);
}

u8 simon_hardware_decryptkeyupdate()
{
#ifdef KEYTESTS  
	u32 l_debugformatting_size = (simonconfigptr->derived_n)/(simonconfigptr->derived_m * 2);
#endif
	// clk_count needs to reset with the bit length of the key.  
	u32 keyword_count=simonconfigptr->derived_m;
	u32 word_size=simonconfigptr->derived_n;
    //printf("WORDSIZE: %i\n",word_size);
	u8 bit3=0; u8 bit2=0; u8 bit_feed=0;
	u8 bit0=0; 
	u8 bit_2intermediate = 0;  //expanded for debugging so that i could check actual values
	u8 bit_3intermediate = 0;
	u8 k_bit0=0;
	u8 k_bit_msb=0;
	u8 bit_key = 0;
	u8 bit_WSm1 = 0;
	/*
	** It would really help to have a copy of my written notes to understand the following
	** This is similar to the encryption except the key bits are all shifted right by 1
	** This means that for the same MUX setting, you have a shift of bits.  Also, the MSB
	** is used for generation.
	*/ 

        if(simonconfigptr->generated_key_mux3==0){
		   bit_2intermediate=arbreg_getbit(p_hardware_key,(word_size*3)+2);		   
		}else{
		  bit_2intermediate=arbreg_getbit(p_hardware_key,(word_size*2)+2);;
		}
		if(simonconfigptr->generated_key_mux4==0){
		   bit_3intermediate=arbreg_getbit(p_hardware_key,(word_size*3)+3);
		}else{
		   bit_3intermediate=arbreg_getbit(p_hardware_key,(word_size*2)+3); 
		}  
		//  the following section is very different from the 3,2 key combinations
		//  There is a detailed write up in my notebook of this, and should be
		//  included in any paper that describes the system.
		if(simonconfigptr->generated_key_mux1==0)
		{
		   bit0=arbreg_getbit(p_hardware_key,(word_size));
		}else
		{
		   bit0=arbreg_getbit(p_hardware_key,0);
		}
		 //the fundamental difference between 4 key words and the others is that you 
		 //have an additional XOR
		  bit_WSm1 = arbreg_getbit(p_hardware_key,(word_size-1));
		  bit3 = bit_3intermediate^bit0;  //bit 1 is actually bit 0 in decrypt, and 4 is 3
		  bit2 = bit_2intermediate^bit_WSm1;   //again, everything shifted right

		//key_xor34 = bit3 ^ bit4;  //key_xor01 represents the tmp<-SR3 k[i-1], SR1 XOR tmp
		k_bit_msb=arbreg_getbit(p_hardware_key,arbreg_getMSBindex(p_hardware_key));
        //generate the logic in a style that uses the mux strobes
      	if(simonconfigptr->generated_crypto_mux0==0)
      	{  k_bit0 = simon_hardware_getZ() ^ 1;;
      	}else
      	{  k_bit0 = 1;  }
      	if(simonconfigptr->generated_crypto_mux1==0)
      	{   bit_feed = k_bit_msb ^ bit2 ^ bit3 ^ k_bit0;
      	}else
      	{   bit_feed = k_bit_msb ^ bit2 ^ bit3;
      	}
      	bit_feed = ~bit_feed;  //invert 
      	bit_feed = bit_feed & 0x01;
        /* The following block shows the key creation iteration and the mux position
        ** DO NOT REMOVE, just comment it out.  word_size is n, so it is the length
        ** of a block word.  The block word plus 1 is as far as the output runs due
        ** to <=, so you can get a feel for how the iteration logic works.
        */ 
#ifdef KEYTESTS       
        if(simonconfigptr->clock_count <=(word_size*3))
      	{
      	   if(simonconfigptr->clock_count%16==0)
      	   {
      	     fprintf(stdout,"k[%02i](%02i) ",simonconfigptr->clock_count,16-(simonconfigptr->clock_count%16));
	         arbreg_debug_dumphexmod(stdout,p_hardware_key,l_debugformatting_size);
      	     fprintf(stdout,"\n");
      	   }
      	   fprintf(stdout,"k[%02i](%02i) ",simonconfigptr->clock_count,16-(simonconfigptr->clock_count%16));
      	   arbreg_debug_dumpbits(stdout,p_hardware_key,1);
      	   fprintf(stdout,"k[%02i] ",simonconfigptr->clock_count);
      	   fprintf(stdout,"b3 b2 b0 WS  z bit_feed\n");
      	   fprintf(stdout,"       %i  %i  %i  %i  %i  %i\n", bit_3intermediate, bit_2intermediate, bit0, bit_WSm1,simon_hardware_getZ(),bit_feed);
      	   fprintf(stdout,"k[%02i] km4 km3 km1 cm8 cm1 cm0\n",simonconfigptr->clock_count);
      	   fprintf(stdout,"       %i   %i   %i   %i   %i   %i  \n", simonconfigptr->generated_key_mux4, simonconfigptr->generated_key_mux3, simonconfigptr->generated_key_mux1, simonconfigptr->generated_crypto_mux8,simonconfigptr->generated_crypto_mux1,simonconfigptr->generated_crypto_mux0); 
      	}
#endif        

         bit_key = bit_feed; //save the bit that will be Ki for the cipher.
         simonconfigptr->generated_key_bit=bit_key;  //the "hardware clock" will feed the bit into the MSB.  

    return(bit_key);  

}



/*
**  u8 simon_hardware_encryptkeyupdate(u32 clk_count)
**
**  The key is cycle for T number of times, and this output is used to encrypt the
**  stream.  The returned value is the current value of key bit.
**  A note is that this function "clock" itself because it updates the shift register
*/
u8 simon_hardware_encryptkeyupdate()
{
	// the clk_count is important because it tell us when to "switch muxes"
	// clk_count needs to reset with the bit length of the key.  
	u32 keyword_count=simonconfigptr->derived_m;
	u32 word_size=simonconfigptr->derived_n;
    //printf("WORDSIZE: %i\n",word_size);
	u8 bit3=0; u8 bit4=0; u8 key_xor34=0; u8 bit_feed=0;
	u8 bit1=0; u8 bit_4intermediate = 0;
	u8 bit_3intermediate = 0;
	u8 k_bit0=0; u8 k_bit_s=0;
	u8 bit_key = 0;

        if(simonconfigptr->generated_key_mux3==0){
		   bit_3intermediate=arbreg_getbit(p_hardware_key,(word_size*3)+3);
		 /*  if(simonconfigptr->clock_count==28)
		   {
		      printf("*** %i%i%i%i\n",arbreg_getbit(p_hardware_key,(word_size*3)+3),arbreg_getbit(p_hardware_key,(word_size*3)+2),arbreg_getbit(p_hardware_key,(word_size*3)+1),arbreg_getbit(p_hardware_key,(word_size*3)+0));
		   }*/		   
		}else{
		  bit_3intermediate=arbreg_getbit(p_hardware_key,(word_size*2)+3);;
		}
		if(simonconfigptr->generated_key_mux4==0){
		   bit_4intermediate=arbreg_getbit(p_hardware_key,(word_size*3)+4);
		}else{
		   bit_4intermediate=arbreg_getbit(p_hardware_key,(word_size*2)+4); 
		}  
		//  the following section is very different from the 3,2 key combinations
		//  There is a detailed write up in my notebook of this, and should be
		//  included in any paper that describes the system.
		if(simonconfigptr->generated_key_mux1==0)
		{
		   bit1=arbreg_getbit(p_hardware_key,(word_size)+1);
		}else
		{
		   bit1=arbreg_getbit(p_hardware_key,1);
		}
	
		 //the fundamental difference between 4 key words and the others is that you 
		 //have an additional XOR
		  bit4 = bit_4intermediate^bit1;
		  bit3 = bit_3intermediate^ arbreg_getbit(p_hardware_key,word_size);  


		key_xor34 = bit3 ^ bit4;  //key_xor01 represents the tmp<-SR3 k[i-1], SR1 XOR tmp
		k_bit0=arbreg_getbit(p_hardware_key,0);
		k_bit_s=(0x01 & ~k_bit0);  //invert the bit, force a bit via mask
		//^^^ bit 0 represents the NOT of the last bit of k, so current key - 2;     
      	//printf("%04u key_xor01[%x] = bit1[%x] ^ bit0[%x]; k_bit_s[%x]=(0x01 & ~k_bit0[~%x,%x]);\n",clk_count,key_xor01,bit1,bit0,k_bit_s,~k_bit0,k_bit0);
      	// this is where things get interesting.  If the clock is < 3, the registers
      	// shift from left to right, but there's a mux that will exist on the upper
      	// register so that data is moved around correctly to address the ROR 3 that
      	// is specified by the SIMON document.
      	
      	
      	
        //generate the logic in a style that uses the mux strobes
      	if(simonconfigptr->generated_crypto_mux0==0)
      	{  k_bit0 = simon_hardware_getZ() ^ 1;;
      	}else
      	{  k_bit0 = 1;  }
      	if(simonconfigptr->generated_crypto_mux1==0)
      	{   bit_feed = k_bit_s ^ key_xor34 ^ k_bit0;
      	}else
      	{   bit_feed = k_bit_s ^ key_xor34;
      	}
        /* The following block shows the key creation iteration and the mux position
        ** DO NOT REMOVE, just comment it out.  word_size is n, so it is the length
        ** of a block word.  The block word plus 1 is as far as the output runs due
        ** to <=, so you can get a feel for how the iteration logic works.
        */ 
#ifdef KEYTESTS      
        if(simonconfigptr->clock_count <=(word_size))
      	{
      	   fprintf(stdout,"k[%02i] ",simonconfigptr->clock_count);
      	   arbreg_debug_dumpbits(stdout,p_hardware_key,1);
      	   fprintf(stdout,"k[%02i] ",simonconfigptr->clock_count);
      	   fprintf(stdout,"b4 b3 b1 b0  z bit_feed\n");
      	   fprintf(stdout,"       %i  %i  %i  %i  %i  %i\n", bit_4intermediate, bit_3intermediate, bit1, k_bit0,simon_hardware_getZ(),bit_feed);
      	   fprintf(stdout,"k[%02i] km4 km3 km1 cm8 cm1 cm0\n",simonconfigptr->clock_count);
      	   fprintf(stdout,"       %i   %i   %i   %i   %i   %i  \n", simonconfigptr->generated_key_mux4, simonconfigptr->generated_key_mux3, simonconfigptr->generated_key_mux1, simonconfigptr->generated_crypto_mux8,simonconfigptr->generated_crypto_mux1,simonconfigptr->generated_crypto_mux0); 

      	}
#endif

         bit_key = bit_feed; //save the bit that will be Ki for the cipher.
         simonconfigptr->generated_key_bit=bit_key;  //the "hardware clock" will feed the bit into the MSB.  

    return(bit_key);  

}
/*
**  simon_hardwareclock():  SHARED FUNCTION
**  The hardware clock is, of course, simulated.  These are the "events" that occur on a 
**  clock.
**  This function is shared between the encryption and decryption routines because it is
**  a central place to write the strobe files.  
*/

void simon_hardwareclock()
{
	u32 u32_n = (simonconfigptr->block_bit_length)/2;  //calculate the n word size 
    
    /*
    **  log the initial state of the probes
    **
    **
    */
    if(simonconfigptr->debug_strobes && SIMON_STROBE_ON)
    {
    //strobe clock
	  
	  fprintf(fp_clock,"%i.0e%i 0\n",simonconfigptr->clock_count,simonconfigptr->clock_magnitude);
	  fprintf(fp_clock,"%i.49e%i 0\n",simonconfigptr->clock_count,simonconfigptr->clock_magnitude);
	  fprintf(fp_clock,"%i.5e%i %s\n",simonconfigptr->clock_count,simonconfigptr->clock_magnitude,simonconfigptr->voltage_ascii);
	  fprintf(fp_clock,"%i.99e%i %s\n",simonconfigptr->clock_count,simonconfigptr->clock_magnitude,simonconfigptr->voltage_ascii);
      
      //create the strobe files
      simon_pwl(fp_lfsr,simon_hardware_getLFSR());  
      simon_pwl(fp_toggle,simon_hardware_gettoggle());
      simon_pwl(fp_toggle,simon_hardware_gettoggle());
      simon_pwl(fp_z,simon_hardware_getZ());

      simon_pwl(fp_key_bit,simonconfigptr->generated_key_bit); 
      simon_pwl(fp_key_mux1,simonconfigptr->generated_key_mux1); 
      simon_pwl(fp_key_mux3,simonconfigptr->generated_key_mux3); 
      simon_pwl(fp_key_mux4,simonconfigptr->generated_key_mux4); 
      
      simon_pwl(fp_crypto_bit,simonconfigptr->generated_crypto_bit); 
      simon_pwl(fp_crypto_mux0,simonconfigptr->generated_crypto_mux0); 
      simon_pwl(fp_crypto_mux1,simonconfigptr->generated_crypto_mux1); 
      simon_pwl(fp_crypto_mux8,simonconfigptr->generated_crypto_mux8);
        
    }
	simon_hardware_counter_inc();
	
	/*
	**  The encryption and decryption are effective inverses.  The encryption section
	**  comes first, and then the decryption section.
	*/
	if((simonconfigptr->simon_configuration & SIMON_ENCRYPT)!=0)
	{
	   //the LFSR and toggle follow a clock based on the key schedule,
	   if((simonconfigptr->clock_count)%u32_n ==0)
	   {
		   simon_hardware_clock_lfsr_encrypt(p_hardware_LFSR);  //clocks the LFSR
		   arbreg_rol(p_hardware_togglebit,1);    //clocks the toggle bit
	   //	fprintf(stdout,"\nLFSR clock(%d):  LFSR=%x, T=%x\n",simonconfigptr->clock_count,simon_hardware_getLFSR(),simon_hardware_gettoggle());
	   }
	   //clock the key generation
	   arbreg_shiftr_insertMSB(p_hardware_key,simonconfigptr->generated_key_bit);
	   //clock the crypto generation
	   arbreg_shiftr_insertMSB(p_hardware_cryptotext,simonconfigptr->generated_crypto_bit);  //update the register with crypto bit
	}else{
	   // the decryption condition follows
	   if((simonconfigptr->clock_count)%u32_n ==0)
	   {
		   simon_hardware_clock_lfsr_decrypt(p_hardware_LFSR);  //clocks the LFSR
		   arbreg_rol(p_hardware_togglebit,1);    //clocks the toggle bit
	   }
	   //clock the key generation
	   arbreg_internal_inst_insertbitl(p_hardware_key,simonconfigptr->generated_key_bit);  //LSB insert
	   arbreg_internal_inst_insertbitl(p_hardware_cryptotext,simonconfigptr->generated_crypto_bit);  //update the register with crypto bit

	}
	
}

/*
**  The "core" function runs the system and retrieves the data in required
**  The encryption and decryption core functions are split, but they don't have to be.
**  The reason is that decryption is completely different in the software sense but 
**  not the hardware sense, so I need MUX control on different counts.
**
*/
void simon_encryptcore_serial()
{
	u32 key_bitclock = 0;  //the key clock is based on the bitwidth of the key
	u32 key_index = 0;
	u32 clock_counter = 0;
	u32 clock_max = simonconfigptr->clock_number;
	u32 l_debugformatting_size = (simonconfigptr->derived_n)/(simonconfigptr->derived_m * 2);
	
	simon_set_encrypt();  //Set the shared functions to use encryption routines.
	
	//The initial state of the LFSR is uniform for encryption, but not decryption
    simon_init_lfsr(0x10,0); //set the correct state of the LFSR
    
    
    //load the key and  from the ascii
	simon_hardware_loadregfromASCII(p_hardware_key,simonconfigptr->key_ascii,(simonconfigptr->derived_n)*(simonconfigptr->derived_m));
	
	//old: load the plain text from the ascii to the p_hardware_cryptotext
    //	simon_hardware_loadregfromASCII(p_hardware_cryptotext,simonconfigptr->test_crypto_ascii,(simonconfigptr->derived_n)*2);	
    
    //new: load the data_in into the crypto word, this means that data_in needs to be 
    // loaded before simon_encryptcore_serial is called. 
    simon_hardware_loadblock();    
    
    //simon_debug_simondata(stdout);
    //simon_debug_printstructure(stdout);
    //arbreg_printhex(stdout,p_hardware_cryptotext); fprintf(stdout,"\n");
    
    /*
    **  The number of rounds, as simonconfigptr->rounds_T is the total for encryption;
    **  however, this program works on "clocks".  If there is not a clock argument
    **  you use the correct number of rounds, otherwise you can clock the system as long
    **  and strangely as you'd like.
    */
    if(clock_max==0)
    {
       clock_max=(simonconfigptr->derived_n) * (simonconfigptr->rounds_T);  //the "base" number of ticks
       /*
       **  A note on clock_max:
       **  When you "print" things to debug them, vs returning a value, you need an extra
       **  clock tick of word width to make the output align; however, this is not
       **  necessary for regular operation.
       */
      // clock_max = clock_max + (simonconfigptr->derived_n);  //the "bonus" tick to push the final word to alignment.
    }
	
	simon_hardware_counter_reset();
	key_bitclock = 0;  
	/* note: clock_max
	** this is ONE more than you'd expect because of the fact that we have
	** an iteration difference.  We start with crypt0, crypt1, but you need 
	** to clock out the first one to generate key 3.
	**
    ** Example for SIMON 128/128 in serial:
    ** c[00] c100000000000000 0000000000000000 
    ** c[01] 0400000000000002 c100000000000000 
    ** c[02] d10000000000000c 0400000000000002 
    ** c[03] bfffffffffffffdd d10000000000000c 
    **
    ** The parallel equivalent by cycle:
    ** c[00] 0400000000000002
	** c[01] d10000000000000c
	** c[02] bfffffffffffffdd
	**
	** Notice the feed through.
	*/
	for (clock_counter=0;clock_counter<clock_max;clock_counter++)
	{
		if(key_bitclock>=(simonconfigptr->derived_n))
        {
        	key_bitclock = 0;
        }
        if (key_bitclock==0)
        {
           //debug control for clock counts
        	switch(key_index)
            {
               case 2:
           //    case 0:
            //     debug_reporting = 1;
             //  break;
               default:
                 debug_reporting = 0;
               break;
            }
            
            //-- I write the state before we increment the hardware, which means that 
            //-- I also must write the state after the final key_index update.
			if(simonconfigptr->debug_writetolog > 0)
			{
#ifndef LATEXLOG			  
			   fprintf(fp_logfile, "z[%02i] ",key_index); 
			   fprintf(fp_logfile,"LFSR:");
			   arbreg_debug_dumpbits(fp_logfile, p_hardware_LFSR,0);   //the bit of the LFSR
			   fprintf(fp_logfile,"toggle:");
			   arbreg_debug_dumpbits(fp_logfile, p_hardware_togglebit,0); 
			   fprintf(fp_logfile,"Z: %i",simon_hardware_getZ() );
			   fprintf(fp_logfile,"\n");
			   fprintf(fp_logfile, "k[%02i] ",key_index);	
			   arbreg_debug_dumphexmod(fp_logfile,p_hardware_key,l_debugformatting_size);
			   fprintf(fp_logfile,"\n");
			   fprintf(fp_logfile, "c[%02i] ",key_index);	
			   arbreg_debug_dumphexmod(fp_logfile,p_hardware_cryptotext,l_debugformatting_size);
			   fprintf(fp_logfile,"\n");
#else
//if we want to format things for the paper.
/*
** \hline
** \multirow{2}{*}{00} &\multirow{2}{*}{1} & key& 0f0e0d0c0b0a09080706050403020100\\ 
** \cline{3-4}
**                                  &   & block &63736564207372656c6c657661727420\\ 
*/
		fprintf(fp_logfile,"\\hline\n");
		fprintf(fp_logfile,"\\multirow{2}{*}{%02i} &\\multirow{2}{*}{%i} & key&",key_index,simon_hardware_getZ());
	    arbreg_debug_dumphexmod(fp_logfile,p_hardware_key,l_debugformatting_size);
	    fprintf(fp_logfile,"\\\\\n");
	    fprintf(fp_logfile,"\\cline{3-4}\n");
	    fprintf(fp_logfile,"& & block &");
	    arbreg_debug_dumphexmod(fp_logfile,p_hardware_cryptotext,l_debugformatting_size);
	    fprintf(fp_logfile,"\\\\\n");
#endif
			}
			key_index++;
       }
        
      /******
      **  The following lines need work:
      **  simon_hardware_updatekey(key_bitclock); 
      **  simon_hardware_encrypt(key_bitclock); 
      **  The fundamental flaw is that the key starts by being loaded.  You'd need
      **  to load the key and the bits in a serial implementation
      **
      **/  
        	simon_hardware_muxcontrolencrypt(key_bitclock);  //generate the mux control lines	
			simon_hardware_encryptkeyupdate();  //update the key based off the clock
            simon_hardware_encryptupdate();    //update the encrypted word			
			simon_hardwareclock();     //tick
			key_bitclock = key_bitclock+1;  //increment the key clock counter

	}
	/******
	**  The rounds are done at this point, so you can print the result or copy it to
	**  the array to send data out.
	**  arbreg_printhex(stdout,p_hardware_cryptotext);  //print the result.
	*/
	arbreg_arraycopy(p_hardware_cryptotext, simondataptr->data_out);  //copy the output to the array
	if(simonconfigptr->debug_writetolog > 0)
	{
#ifndef LATEXLOG
	   fprintf(fp_logfile, "z[%02i] ",key_index); 
	   fprintf(fp_logfile,"LFSR:");
	   arbreg_debug_dumpbits(fp_logfile, p_hardware_LFSR,0);   //the bit of the LFSR
	   fprintf(fp_logfile,"toggle:");
	   arbreg_debug_dumpbits(fp_logfile, p_hardware_togglebit,0); 
	   fprintf(fp_logfile,"Z: %i",simon_hardware_getZ() );
	   fprintf(fp_logfile,"\n");
	   fprintf(fp_logfile, "k[%02i] ",key_index);	
	   arbreg_debug_dumphexmod(fp_logfile,p_hardware_key,l_debugformatting_size);
	   fprintf(fp_logfile,"\n");
	   fprintf(fp_logfile, "c[%02i] ",key_index);	
	   arbreg_debug_dumphexmod(fp_logfile,p_hardware_cryptotext,l_debugformatting_size);
	   fprintf(fp_logfile,"\n");
#else
//if we want to format things for the paper.
/*
** \hline
** \multirow{2}{*}{00} &\multirow{2}{*}{1} & key& 0f0e0d0c0b0a09080706050403020100\\ 
** \cline{3-4}
**                                  &   & block &63736564207372656c6c657661727420\\ 
*/
		fprintf(fp_logfile,"\\hline\n");
		fprintf(fp_logfile,"\\multirow{2}{*}{%02i} &\\multirow{2}{*}{%i} & key&",key_index,simon_hardware_getZ());
	    arbreg_debug_dumphexmod(fp_logfile,p_hardware_key,l_debugformatting_size);
	    fprintf(fp_logfile,"\\\\\n");
	    fprintf(fp_logfile,"\\cline{3-4}\n");
	    fprintf(fp_logfile,"& & block &");
	    arbreg_debug_dumphexmod(fp_logfile,p_hardware_cryptotext,l_debugformatting_size);
	    fprintf(fp_logfile,"\\\\\n");
#endif

	}    
	
	//fprintf(stdout,"\n");
	//	fprintf(stdout,"\ndumpkey:\n");
	//simon_debug_hardware_dumpkey();
	
	//fprintf(stdout,"\n");
	
}



i32 simon_hardware_clock_lfsr_decrypt(arbitrary_register *p_arbreg)
{
	u8 bit0=0;  //right most bit
	u8 bit1=0;
	u8 bit2=0;
	u8 bit3=0;
	u8 opres0=0;
	u8 opres1=0;
	u8 opres4=0;
	
    //unlike the encryption version, these are logic that counts backwards

		  bit3=arbreg_getbit(p_arbreg, 3);  
		  bit0=(arbreg_getbit(p_arbreg, 0));
		  opres4= bit3^bit0;
		  bit1=arbreg_getbit(p_arbreg, 4);  
		  opres1= opres4^bit1;
		  arbreg_ror(p_arbreg,1);
		  arbreg_setbit(p_arbreg, 4,opres4);
		  arbreg_setbit(p_arbreg, 3,opres1);		

	return(0);
}

void simon_decryptcore_serial()
{
	u32 u32_blocklength=simonconfigptr->block_bit_length;
	u32 u32_keylength=simonconfigptr->key_bit_length;		
	u8  l_lfsrinit = 0;
	u8  l_toggle=0;
	u32 key_bitclock = 0;  //the key clock is based on the bitwidth of the key
	u32 key_index = 0;
	u32 clock_counter = 0;
	u32 clock_max = simonconfigptr->clock_number;
	u32 l_debugformatting_size = (simonconfigptr->derived_n)/(simonconfigptr->derived_m * 2);

	simon_set_decrypt();  //make sure the shared functions are in the correct mode.
	

			l_lfsrinit=0x10;  //10000
			l_toggle=1;

	
	simon_init_lfsr(l_lfsrinit,l_toggle);  //set the initial state of the LFSR based on the series
    //load the key and  from the ascii
	simon_hardware_loadregfromASCII(p_hardware_key,simonconfigptr->key_ascii,(simonconfigptr->derived_n)*(simonconfigptr->derived_m));
	simon_hardware_loadblock();    
 
	
	
	
	
    /*
    **  The number of rounds, as simonconfigptr->rounds_T is the total for encryption;
    **  however, this program works on "clocks".  If there is not a clock argument
    **  you use the correct number of rounds, otherwise you can clock the system as long
    **  and strangely as you'd like.
    */
    if(clock_max==0){
       clock_max=(simonconfigptr->derived_n) * (simonconfigptr->rounds_T);  //the "base" number of ticks
    }
    for (clock_counter=0;clock_counter<clock_max;clock_counter++)
	{
		if(key_bitclock>=(simonconfigptr->derived_n))
        {
        	key_bitclock = 0;
        }
        if (key_bitclock==0)
        {
            /*
            **  Write the state to the log file
            */
			if(simonconfigptr->debug_writetolog > 0)
			{
			   fprintf(fp_logfile, "z[%02i] ",key_index); 
			   fprintf(fp_logfile,"LFSR:");
			   arbreg_debug_dumpbits(fp_logfile, p_hardware_LFSR,0);   //the bit of the LFSR
			   fprintf(fp_logfile,"toggle:");
			   arbreg_debug_dumpbits(fp_logfile, p_hardware_togglebit,0); 
			   fprintf(fp_logfile,"Z: %i",simon_hardware_getZ() );
			   fprintf(fp_logfile,"\n");
			   fprintf(fp_logfile, "k[%02i] ",key_index);	
			   arbreg_debug_dumphexmod(fp_logfile,p_hardware_key,l_debugformatting_size);
			   fprintf(fp_logfile,"\n");
			   fprintf(fp_logfile, "c[%02i] ",key_index);	
			   arbreg_debug_dumphexmod(fp_logfile,p_hardware_cryptotext,l_debugformatting_size);
			   fprintf(fp_logfile,"\n");
			}
		//TEMPORARY debug output
		 ///if((simonconfigptr->clock_count)%u32_n ==0)	
		 // fprintf(stdout, "z[%02i] ",key_index);	
	     // arbreg_debug_dumpbits(stdout,p_hardware_LFSR,1);
	     
		//	
			key_index++;		
       }
       
       simon_hardware_muxcontroldecrypt(key_bitclock);  //decryption mux control
       simon_hardware_decryptkeyupdate();
       simon_hardware_decryptupdate();    //update the encrypted word			
	   simon_hardwareclock();
	   key_bitclock = key_bitclock+1;  //increment the key clock counter once all functions complete
	}
	// fprintf(stdout, "z[%02i] ",key_index);	
	// arbreg_debug_dumpbits(stdout,p_hardware_LFSR,1);
    
    arbreg_arraycopy(p_hardware_cryptotext, simondataptr->data_out);  //copy the output to the array

	if(simonconfigptr->debug_writetolog > 0)
	{
	   fprintf(fp_logfile, "z[%02i] ",key_index); 
	   fprintf(fp_logfile,"LFSR:");
	   arbreg_debug_dumpbits(fp_logfile, p_hardware_LFSR,0);   //the bit of the LFSR
	   fprintf(fp_logfile,"toggle:");
	   arbreg_debug_dumpbits(fp_logfile, p_hardware_togglebit,0); 
	   fprintf(fp_logfile,"Z: %i",simon_hardware_getZ() );
	   fprintf(fp_logfile,"\n");
	   fprintf(fp_logfile, "k[%02i] ",key_index);	
	   arbreg_debug_dumphexmod(fp_logfile,p_hardware_key,l_debugformatting_size);
	   fprintf(fp_logfile,"\n");
	   fprintf(fp_logfile, "c[%02i] ",key_index);	
	   arbreg_debug_dumphexmod(fp_logfile,p_hardware_cryptotext,l_debugformatting_size);
	   fprintf(fp_logfile,"\n");
	}
    
    
}





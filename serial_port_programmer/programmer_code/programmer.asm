


						list P=PIC16F648A

						include "P16F648A.INC"

						__config _INTOSC_OSC_NOCLKOUT & _WDT_OFF & _MCLRE_OFF & _LVP_OFF

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Data
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PROTOCOL_VERSION		equ		1

; Error codes
ERROR_OVERFLOW			equ		'1'
ERROR_FRAMING			equ		'2'
ERROR_VERIFY			equ		'3'
ERROR_BAD_COMMAND		equ		'4'

; Pins
VPP						equ		0		; RA0
nVDD					equ 	1		; RA1
PGM_CLOCK				equ 	2		; RA2
PGM_DATA				equ		3		; RA3

; Commands to target  (rightmost 6 bits)
CMD_BULK_ERASE_PROGRAM			equ		0x09
CMD_LOAD_DATA_PROGRAM			equ		0x02
CMD_INCREMENT_ADDR				equ		0x06
CMD_BEGIN_PROGRAM_ONLY_CYCLE	equ		0x08
CMD_READ_PROGRAM_MEMORY			equ		0x04
CMD_LOAD_CONFIGURATION			equ		0x00

						org		0x20

command_buffer:			res		1
program_size_hi:		res		1
program_size_lo:		res		1
checksum_hi:			res		1
checksum_lo:			res		1
word_shift_register:	res		1	; Temporary for send_to_target
word_shift_count:		res		1	; Temporary for send_to_target
program_word_hi:		res		1
program_word_lo:		res		1
error_flag:				res		1
delay_interval:			res		1	; Temporary used by delay
delay_sub_count:		res		1	; Temporary used by delay
loop_count:				res		1
verify_word_hi:			res		1
verify_word_lo:			res		1

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Program
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

						; Vector table
						org 	0
						goto 	initialize		; Reset vector
						nop
						nop
						nop
						nop

initialize:				movlw	0x07			; Turn comparters off and enable pins for IO
						movwf	CMCON

						bsf		STATUS,RP0		; Switch to page 1
						movlw	b'11111100'		; Float clock and data
						movwf	TRISA
						movlw	b'11111111'
						movwf	TRISB
						clrf	VRCON			; Turn off voltage reference module (RA2 is GPIO)

						; Set up the serial port
						movlw	.25		; 9600 baud
						movwf	SPBRG
						bsf		TXSTA, BRGH	; High speed
						bcf		TXSTA, SYNC
						bcf		STATUS,RP0	; Page 0
						bsf		RCSTA, SPEN
						bsf		RCSTA, CREN
						bsf		STATUS, RP0	; Page 1
						bsf		TXSTA, TXEN

						bcf		STATUS, RP0	; Page 0

						;	clrf	CCP1CON					; Disable PWM output
						;	bcf		T1CON, T1OSCEN			; Make B6 be a GPIO (not Timer 1 output)

						; Initially turn target off
						bcf		PORTA, VPP
						bsf		PORTA, nVDD

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Command interpreter
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

command_loop:			clrf	error_flag

						call	recv_from_host	; Get the next command
						movwf	command_buffer	; Stash

						; switch (command)
						; case 'E':   Erase program memory
						sublw	'E'
						btfsc	STATUS, Z	; Equal?
						goto	cmd_erase_flash

						; case 'C':	  Write configuration word
						movfw	command_buffer
						sublw	'C'
						btfsc	STATUS, Z	; Equal?
						goto	cmd_write_config_word

						; case 'W':   Write program memory
						movfw	command_buffer
						sublw	'W'
						btfsc	STATUS, Z	; Equal?
						goto	cmd_write_program

						; case 'V':   Get version
						movfw	command_buffer
						sublw	'V'
						btfsc	STATUS, Z	; Equal?
						goto	cmd_print_version

						; case 'P':   Enter programming mode
						movfw	command_buffer
						sublw	'P'
						btfsc	STATUS, Z	; Equal?
						goto	cmd_enter_program_mode

						; case 'X':   Exit programming mode
						movfw	command_buffer
						sublw	'X'
						btfsc	STATUS, Z	; Equal?
						goto	cmd_exit_program_mode

						; case 'T':	Test command
						movfw	command_buffer
						sublw	'T'
						btfsc	STATUS, Z
						goto	cmd_test

						; case 'I': I/O control
						movfw	command_buffer
						sublw	'I'
						btfsc	STATUS, Z
						goto	cmd_io

						; Command is unrecognized.
						movlw	'E'
						call	send_to_host
						movlw	ERROR_BAD_COMMAND
						call	send_to_host
						goto	command_loop


;;;;; Erase Flash ;;;;;;;;;;;;;;;;;;;;;;
cmd_erase_flash:		movlw	CMD_BULK_ERASE_PROGRAM
						call	send_to_target6

						; Wait Tera (6ms)
						movlw	.120
						call	delay

						; Send ack
						movlw	'+'
						call	send_to_host
						goto	command_loop

;;;;; Write Config Word ;;;;;;;;;;;;;;;;;;;;;;
cmd_write_config_word:	call	recv_from_host
						movwf	program_word_hi
						call	recv_from_host
						movwf	program_word_lo

						; Load data for configuration memory
						; Advances the PC to the start of configuration memory (0x2000-0x200F)
						; and loads the data for the first ID location.
						movlw	CMD_LOAD_CONFIGURATION
						call	send_to_target6

						; 0x7ffe as data of command
						movlw	0xfe
						call	send_to_target8
						movlw	0x7f
						call	send_to_target8

						; Advance to address 2007
						movlw	7
						movwf	loop_count
increment_loop:			movlw	CMD_INCREMENT_ADDR		; Increment address
						call	send_to_target6
						decfsz	loop_count, f
						goto	increment_loop

						; Now write the configuration word
						call 	write_program_word
						btfsc	error_flag, 0			; Did we write the config word?
						goto	program_error			; Nope, bail

						movlw	'+'
						call	send_to_host

						goto	command_loop

;;;;; Write Program Memory ;;;;;;;;;;;;;;;;;;;;;;
cmd_write_program:			; Get the program size
						call	recv_from_host
						movwf	program_size_hi
						call	recv_from_host
						movwf	program_size_lo

						clrf	checksum_hi
						clrf	checksum_lo

						movlw	'+'	; go ahead
						call	send_to_host


get_instruction_loop:	; Update counters and loop
						movlw	1
						subwf	program_size_lo, f
						btfsc	STATUS, C
						goto	get_instruction

						; low counter has wrapped, decrement high counter
						movlw	1
						subwf	program_size_hi, f
						btfsc	STATUS, C
						goto	get_instruction

						; that's it
						goto	instruction_loop_done


get_instruction:		call	recv_from_host	; Get highword
						movwf	program_word_hi

						; Update checksum
						addwf	checksum_lo, f
						movfw	checksum_lo
						addwf	checksum_hi, f

						call	recv_from_host 	; Get lowword
						movwf	program_word_lo

						; update checksum
						addwf	checksum_lo, f
						movfw	checksum_lo
						addwf	checksum_hi, f

						call	write_program_word		; Do it
						btfsc	error_flag, 0			; Check if an error occured
						goto	program_error			; An error occured, bail

						movlw	'+'
						call	send_to_host
						goto	get_instruction_loop


instruction_loop_done:	; Write checksum
						movlw	'D'
						call	send_to_host
						movfw	checksum_hi
						call	send_to_host
						movfw	checksum_lo
						call	send_to_host
						goto	command_loop

cmd_print_version:		movlw	PROTOCOL_VERSION
						call	send_to_host
						goto	command_loop

program_error:			; Automatically exit programming mode
						call	exit_program_mode

						; Send error to host
						movlw	'E'
						call	send_to_host
						movlw	ERROR_VERIFY
						call	send_to_host

						; XXX send the offending word to the host
						movfw	verify_word_hi
						call	send_to_host
						movfw	verify_word_lo
						call	send_to_host

						goto command_loop

;;;;; Enter programming mode ;;;;;;;;;;;;;;;;;;;
cmd_enter_program_mode:	; Turn clock and data lines, which are currently floating, into
						; outputs and drive them low.
						bsf		STATUS, RP0		; Switch to page 1
						bcf		TRISA, PGM_CLOCK
						bcf		TRISA, PGM_DATA
						bcf		STATUS, RP0		; Back to page 0
						bcf		PORTA, PGM_CLOCK
						bcf		PORTA, PGM_DATA
						goto	$+1
						goto	$+1

						bsf		PORTA, VPP				; Programming voltage goes high

						; Wait Tppdp (5 uS)
						goto	$+1
						goto	$+1
						goto	$+1

						bcf		PORTA, nVDD				; nVDD goes high

						; Wait Thld0 (5 uS)
						goto	$+1
						goto	$+1
						goto	$+1

						; Send acknowledgement
						movlw	'+'
						call	send_to_host
						goto	command_loop

;;;; Exit programming mode ;;;;;;;;;;;;;;;;;;;;;;;;;
cmd_exit_program_mode:	call	exit_program_mode
						movlw	'+'
						call	send_to_host
						goto	command_loop


;;;;;; Test command ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
cmd_test:				movlw	'+'
						call	send_to_host
						goto 	command_loop


;;;;;; I/O command ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
cmd_io:					call	recv_from_host	; Get the next command
						movwf	command_buffer	; Stash

						sublw	'1'
						btfsc	STATUS, Z	; Equal?
						bsf		PORTA, VPP	; Yes, set bit

						movfw	command_buffer
						sublw	'2'
						btfsc	STATUS, Z
						bcf		PORTA, nVDD

						movfw	command_buffer
						sublw	'3'
						btfsc	STATUS, Z
						bsf		PORTA, PGM_CLOCK

						movfw	command_buffer
						sublw	'4'
						btfsc	STATUS, Z
						bsf		PORTA, PGM_DATA

						movfw	command_buffer
						sublw	'5'
						btfsc	STATUS, Z	; Equal?
						bcf		PORTA, VPP	; Yes, set bit

						movfw	command_buffer
						sublw	'6'
						btfsc	STATUS, Z
						bsf		PORTA, nVDD

						movfw	command_buffer
						sublw	'7'
						btfsc	STATUS, Z
						bcf		PORTA, PGM_CLOCK

						movfw	command_buffer
						sublw	'8'
						btfsc	STATUS, Z
						bcf		PORTA, PGM_DATA

						movlw	'+'
						call	send_to_host

						goto	command_loop


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Exit programming mode and set the programmer back to a known state.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

exit_program_mode:		; Float clock and data so circuit can operate normally
						bsf		STATUS, RP0		; Switch to page 1
						bsf		TRISA, PGM_CLOCK
						bsf		TRISA, PGM_DATA
						bcf		STATUS, RP0		; Back to page 0

						bcf		PORTA, VPP
						bsf		PORTA, nVDD
						return

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Send data over the UART to the host.  Data to send should be loaded into W.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

send_to_host:			bsf		STATUS, RP0		; Page 1
xmit_wait_loop:			btfss	TXSTA, TRMT
						goto	xmit_wait_loop	; wait for space in transmitter
						bcf		STATUS, RP0		; Page 0
						movwf	TXREG
						return

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Wait for data to arrive over the UART from the host.  Returned data will
;; be placed in w
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

recv_from_host:
wait_for_data:			btfss	PIR1, RCIF
						goto	wait_for_data

						btfsc	RCSTA, OERR
						goto	handle_overflow
						btfsc	RCSTA, FERR
						goto	handle_framing_error
						movfw	RCREG
						return

handle_overflow:		movlw	'E'
						call	send_to_host
						movlw	ERROR_OVERFLOW
						call	send_to_host
						goto	wait_for_data		; Wait for a valid data byte

handle_framing_error:	movlw	'E'
						call	send_to_host
						movlw	ERROR_FRAMING
						call	send_to_host
						goto	wait_for_data		; Wait for a valid data byte


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Write a program word to flash and verify that it was written correctly
;;
;;   program_word_hi (in)       High 8 bits of program word to write
;;   program_word_lo (in)       Low 8 bits of program word to write
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

write_program_word:		movlw	CMD_LOAD_DATA_PROGRAM
						call	send_to_target6

						nop		; Wait Tdly2

						; Write data (16 bits)
						movfw	program_word_lo
						call	send_to_target8
						movfw	program_word_hi
						call	send_to_target8

						movlw	CMD_BEGIN_PROGRAM_ONLY_CYCLE	; Begin programming only cycle
						call	send_to_target6

						; Wait Tprog (2.5ms)
						movlw 	.50					; 50 * 50 us = 2.5ms
						call	delay

						; Read data from program memory
						movlw	CMD_READ_PROGRAM_MEMORY
						call	send_to_target6

						; Turn the data line into an input so we can read back from
						; the target
						bsf		STATUS, RP0		; Switch to page 1
						bsf		TRISA, PGM_DATA	; Turn data into an input
						bcf		STATUS, RP0		; Back to page 0

						nop		; Wait Tdly2

						call	recv_from_target8
						movwf	verify_word_lo
						bcf		verify_word_lo, 0	; Ignore low bit
						call	recv_from_target8
						movwf	verify_word_hi
						bcf		verify_word_hi, 7	; Ignore high bit

						; Turn the data line back into an output
						bsf		STATUS, RP0		; Switch to page 1
						bcf		TRISA, PGM_DATA	; Turn data back into an output
						bcf		STATUS, RP0		; Back to page 0

						; Verify MSB
						movfw	verify_word_hi
						xorwf	program_word_hi, w	; Compare against low word
						btfss	STATUS, Z
						goto	wpw_error			; Did not equal

						; Verify LSB
						movfw	verify_word_lo
						xorwf	program_word_lo, w	; Compare against high word
						btfss	STATUS, Z
						goto	wpw_error			; Did not equal

						; Increment address
						movlw	CMD_INCREMENT_ADDR
						call	send_to_target6

						nop		; Wait Tdly2
						return

wpw_error:				bsf		error_flag, 0

						return


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Write 6 bits to target
;; PGM_CLOCK is left low on exit
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

send_to_target6:		movwf	word_shift_register
						movlw	6
						movwf	word_shift_count
						goto	send_to_target_common

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Write 8 bits to target
;; PGM_CLOCK is left low on exit
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

send_to_target8:		movwf	word_shift_register
						movlw	8
						movwf	word_shift_count
						goto	send_to_target_common

; Common part of send_to_target6 and send_to_target8
; Bit bang data to the target.  Data is latched on the falling edge of PGM_CLOCK.  Command
; and data words are clocked in LSb first.
send_to_target_common:	bsf		PORTA, PGM_CLOCK		; PGM_CLOCK goes high
						btfss	word_shift_register, 0	; Is the next bit to shift a 1?
						goto	not_on					; It's clear
						bsf 	PORTA, PGM_DATA			; Set data bit
						goto	bit_set_done
not_on:					bcf		PORTA, PGM_DATA			; Clear data bit
bit_set_done:			rrf		word_shift_register, f
						bcf		PORTA, PGM_CLOCK			; PGM_CLOCK goes low
						decfsz	word_shift_count, f		; Decrement loop count, drop out if done
						goto	send_to_target_common   ; Next bit
						return


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Read 8 bits of data from the synchronous serial interface
;; The PGM_CLOCK must start out low on entry to this function.  It will be low on
;; exit.  This modifies word_shift_count and word_shift_register.
;; The received octet will be returned in W
;; PGM_CLOCK is left low on exit
;; It is expected that PGM_DATA will already be configured as an input when this
;; is called.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

recv_from_target8:		movlw	8						; Count of bits to read (8)
						movwf	word_shift_count		; Stash in count register
bit_loop:				bsf		PORTA, PGM_CLOCK		; PGM_CLOCK goes high

						; Need to wait Tdly3.  These two instructions are adequate delay...
						bcf		STATUS, C				; Clear carry bit
						rrf		word_shift_register, f	; Rotate bits

						btfsc	PORTA, PGM_DATA			; Skip next if data is clear
						bsf		word_shift_register, 7	; Data is set, so set most significant bit
						bcf		PORTA, PGM_CLOCK			; PGM_CLOCK goes low
						decfsz	word_shift_count, f		; Decrement loop count, drop out if done
						goto	bit_loop				; Do next bit
						movfw	word_shift_register
						return

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Delay for the specified amount of time.  The number of 50 uS intervals is
;; passed in in W.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

delay:					movwf	delay_interval
delay_loop1:			movlw	.15						; 1 cycle
						movwf	delay_sub_count			; 1 cycle
delay_loop2:			decfsz	delay_sub_count, f		; 1 cycle
						goto	delay_loop2				; 2 cycles
						decfsz	delay_interval, f		; 1 cycle
						goto	delay_loop1				; 2 cycles
						return

						end


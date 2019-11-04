// -------------------------------------------------------------------------------------------------

#include "codehook.h"

#define length(a) (sizeof(a) / sizeof(a[0]))

#define LONGJMP_CODE_SIZE	5

// -------------------------------------------------------------------------------------------------

static unsigned long instruction_size_table[] =
{
	0x03000002, 0x04040100, 0x05050800, 0x07060000, 0x0B080002,
	0x0C0C0100, 0x0D0D0800, 0x0E0E0000, 0x13100002, 0x14140100,
	0x15150800, 0x17160000, 0x1B180002, 0x1C1C0100, 0x1D1D0800,
	0x1F1E0000, 0x23200002, 0x24240100, 0x25250800, 0x27270000,
	0x2B280002, 0x2C2C0100, 0x2D2D0800, 0x2F2F0000, 0x33300002,
	0x34340100, 0x35350800, 0x37370000, 0x3B380002, 0x3C3C0100,
	0x3D3D0800, 0x613F0000, 0x62620002, 0x68680800, 0x69690802,
	0x6A6A0100, 0x6B6B0102, 0x7F700108, 0x80800102, 0x81810802,
	0x83820102, 0x8E840002, 0x8F8F0006, 0x99900000, 0x9A9A0A08,
	0x9F9C0000, 0xA3A08000, 0xA7A40000, 0xA9A80100, 0xAFAA0000,
	0xB7B00100, 0xBFB80800, 0xC1C00102, 0xC2C20208, 0xC3C30008,
	0xC5C40002, 0xC6C60106, 0xC7C70806, 0xC8C80300, 0xC9C90000,
	0xCACA0200, 0xCBCB0000, 0xCCCC0008, 0xCDCD0108, 0xCFCE0008,
	0xD3D00002, 0xD5D40100, 0xD7D60000, 0xDFD80002, 0xE3E00108,
	0xE9E80808, 0xEAEA0A08, 0xEBEB0108, 0xF5F10000, 0xF6F60106,
	0xF6F60002, 0xF7F70806, 0xF7F70002, 0xFDF80000, 0xFEFE0006,
	0xFEFE0016, 0xFFFF0006, 0xFFFF0016, 0xFFFF002E, 0xFFFF003E,
	0xFFFF004E, 0xFFFF005E, 0xFFFF0066, 0x8F800809, 0x9F900003,
	0xA1A00001, 0xA3A30003, 0xA4A40103, 0xA5A50003, 0xA9A80001,
	0xABAB0003, 0xACAC0103, 0xB7AD0003, 0xBABA0103, 0xC1BC0003,
	0xCFC80001,
};

// -------------------------------------------------------------------------------------------------

static int sizeof_instruction(unsigned char *buf)
{
	int i, flag_two_byte, size = 0, opsize, is_prefix_byte;
	int op_size_ovr = 0, adr_size_ovr = 0, is_two_byte = 0;
	unsigned char opcode, start, end, modrm, sib;
	unsigned char mode, rmem, regi, subcode, subcode2;
	unsigned long data;

	// parse prefices
	do {
		is_prefix_byte = 1;
		switch(*buf) {
			case 0x66:
				op_size_ovr = 1;
				break;
			case 0x67:
				adr_size_ovr = 1;
				break;
			case 0x26: case 0x2E: case 0x36:
			case 0x3E: case 0x64: case 0x65:
			case 0xF0: case 0xF2: case 0xF3:
			case 0x9B:
				break;
			default:
				is_prefix_byte = 0;
				break;
		}
		if(is_prefix_byte) {
			buf++;
			size++;
		}
	} while( is_prefix_byte && (size < 16) );

	if(*buf == 0x0F) {
		is_two_byte = 1;
		buf++;
		size++;
	}

	// lookup opcode table
	opcode = *(buf++);
	size++;

	data = 0;
	for(i = 0; i < length(instruction_size_table); ++i) {
		start = (unsigned char)((instruction_size_table[i] >> 16) & 0xFF);
		end = (unsigned char)((instruction_size_table[i] >> 24) & 0xFF);
		flag_two_byte = instruction_size_table[i] & 1;
		if( (opcode >= start) && (opcode <= end) && (is_two_byte == flag_two_byte) ) {
			if((instruction_size_table[i] & 6) == 6) {
				subcode = (*buf >> 3) & 7;
				subcode2 = (unsigned char)((instruction_size_table[i] >> 4) & 7);
				if(subcode == subcode2) {
					data = instruction_size_table[i];
					break;
				}
			} else {
				data = instruction_size_table[i];
				break;
			}
		}
	}

	if(data == 0) {
		return 0;
	}

	// parse modrm
	if(data & 2) {
		modrm = *(buf++);
		size++;

		// unpack modrm
		mode = modrm >> 6;
		regi = (modrm >> 3) & 7;
		rmem = modrm & 7;

		// calculate modrm's operands size
		opsize = 0;

		// modrm mode0/6 for 16b, mode0/5 for 32b (reg, [imm16/32])
		// or mode2 (reg, [reg + imm16/32])
		if( ((mode == 0) && (rmem == (adr_size_ovr ? 6 : 5))) || (mode == 2) ) {
			opsize += adr_size_ovr ? 2 : 4;
		}
		// modrm mode 1 (reg, [reg+imm8])
		else if(mode == 1) {
			opsize++;
		}

		// calculate sib's operands size
		if( (mode != 3) && (!adr_size_ovr) && (rmem == 4) ) {
			sib = *(buf++);
			opsize++;
			// sib have imm32
			if( (mode == 0) && ((sib & 7) == 5) )
				opsize += 4;
		}

		size += opsize;
	}

	// calc static operands size
	size += (data >> 8) & 7;
	size += (data >> 12) & 7;
	if((data >> 8) & 8) size += op_size_ovr ? 2 : 4;
	if((data >> 12) & 8) size += adr_size_ovr ? 2 : 4;

	// negative result for branch instructions
	if(data & 8) size = -size;

	return size;
}

// -------------------------------------------------------------------------------------------------

static void write_longjmp(unsigned char *location, void* targetp)
{
	unsigned long origin, target;

	origin = (unsigned long)location + 5;
	target = (unsigned long)targetp;

	*location = 0xE9;
	*(unsigned long*)(location + 1) = target - origin;
}

// -------------------------------------------------------------------------------------------------

static void* check_longjmp(unsigned char *location)
{
	unsigned long origin, target;

	if(*location == 0xE9) {
		origin = (unsigned long)location + 5;
		target = origin + *(unsigned long*)(location + 1);
		return (void*)target;
	}
	return 0;
}

// -------------------------------------------------------------------------------------------------

static void write_nops(unsigned char *buf, unsigned long offset, unsigned long count)
{
	memset(buf + offset, 0x90, count);
}

// -------------------------------------------------------------------------------------------------

static DWORD set_protection(void *region, DWORD size, DWORD protect)
{
	DWORD oldprotect;

	if(VirtualProtect(region, size, protect, &oldprotect))
		return oldprotect;
	return 0;
}

// -------------------------------------------------------------------------------------------------

CODE_HOOK_ERROR code_hook_install(code_hook_t *phook, void *location, void *target, int vector)
{
	int size, s;
	unsigned char *tmp;
	void *next_hook, *ret_pos;
	DWORD protect;

	write_nops((void*)phook, 0, sizeof(code_hook_t));

	phook->is_active = 0;
	phook->is_vectored = 0;
	phook->location = location;
	phook->target = target;
	phook->codesize = 0;

	if(IsBadReadPtr(location, CODE_HOOK_MAX_MOVED_CODE))
		return CODE_HOOK_BADREADPTR;

	// hook vectoring mode
	if((next_hook = check_longjmp(location)) != NULL) {

		if(!vector) return CODE_HOOK_CONFLICT;

		// copy next hook bytes for futher checks
		if(IsBadReadPtr(next_hook, sizeof(phook->checkvec)))
			return CODE_HOOK_BADREADPTR;
		memcpy(phook->checkvec, next_hook, sizeof(phook->checkvec));

		// install vectored hook
		if((protect = set_protection(location, LONGJMP_CODE_SIZE, PAGE_EXECUTE_READWRITE)) == 0)
			return CODE_HOOK_CANTPROTECT;
		write_longjmp(phook->code, next_hook);
		write_longjmp(location, target);
		set_protection(location, LONGJMP_CODE_SIZE, protect);

		phook->is_active = 1;
		phook->is_vectored = 1;
		phook->codesize = LONGJMP_CODE_SIZE;
	}

	// hook install mode
	else {

		tmp = location;

		// check for "call [imm]" or "jmp [imm]" hook,
		// treat it as nomrmal code (dangerous with volatile imm!!!)
		if( (tmp[0] == 0xff) && ((tmp[1] == 0x15) || (tmp[1] == 0x25)) ) {
			if( (!vector) || (!CODE_HOOK_ENABLE_UNSAFE_VECTORING) )
				return CODE_HOOK_CONFLICT;
			size = 6;
		}

		// count bytes to move
		else {
			for(size = 0; size < LONGJMP_CODE_SIZE; size += s) {
				s = sizeof_instruction(tmp);
				if(s <= 0) return CODE_HOOK_BADCODE;
				tmp += s;
			}
		}

		// install hook
		if((protect = set_protection(location, size, PAGE_EXECUTE_READWRITE)) == 0)
			return CODE_HOOK_CANTPROTECT;
		memcpy(phook->code, location, size);
		ret_pos = (void*)((unsigned long)location + size);
		write_longjmp((unsigned char*)(phook->code) + size, ret_pos);
		write_longjmp(location, target);
		write_nops(location, LONGJMP_CODE_SIZE, size - LONGJMP_CODE_SIZE);
		set_protection(location, size, protect);

		phook->is_active = 1;
		phook->codesize = size;
	}

	// save modified bytes for futher checking
	memcpy(phook->check, location, sizeof(phook->check));

	return CODE_HOOK_OK;
}

// -------------------------------------------------------------------------------------------------

CODE_HOOK_ERROR code_hook_verify(code_hook_t *phook)
{
	if( (!phook) || (!phook->is_active) )
		return CODE_HOOK_ABSENCE;
	if(IsBadReadPtr(phook->location, sizeof(phook->check)))
		return CODE_HOOK_BADREADPTR;
	if(memcmp(phook->location, phook->check, sizeof(phook->check)) != 0)
		return CODE_HOOK_CHANGED;
	return CODE_HOOK_OK;
}

// -------------------------------------------------------------------------------------------------

CODE_HOOK_ERROR code_hook_remove(code_hook_t *phook)
{
	CODE_HOOK_ERROR error;
	DWORD protect;
	void *next_hook;

	if((error = code_hook_verify(phook)) != CODE_HOOK_OK)
		return error;

	if(phook->is_vectored) {
		if((next_hook = check_longjmp(phook->code)) == NULL)
			return CODE_HOOK_CHANGED;
		if(IsBadReadPtr(next_hook, sizeof(phook->checkvec)))
			return CODE_HOOK_CHANGED;
		if(memcmp(next_hook, phook->checkvec, sizeof(phook->checkvec)) != 0)
			return CODE_HOOK_CHANGED;
		if((protect = set_protection(phook->location, phook->codesize, PAGE_EXECUTE_READWRITE)) == 0)
			return CODE_HOOK_CANTPROTECT;
		write_longjmp(phook->location, next_hook);
		set_protection(phook->location, phook->codesize, protect);
	}

	else {
		if((protect = set_protection(phook->location, phook->codesize, PAGE_EXECUTE_READWRITE)) == 0)
			return CODE_HOOK_CANTPROTECT;
		memcpy(phook->location, phook->code, phook->codesize);
		set_protection(phook->location, phook->codesize, protect);
	}

	memset(phook, 0, sizeof(code_hook_t));

	return CODE_HOOK_OK;
}

// -------------------------------------------------------------------------------------------------

void *code_hook_get_oep(code_hook_t *phook)
{
	if( (phook == NULL) || (!phook->is_active) )
		return NULL;
	return phook->code;
}

// -------------------------------------------------------------------------------------------------

static DWORD rva_to_file_position(HANDLE hmodule, DWORD rva)
{
	IMAGE_DOS_HEADER *pmz;
	IMAGE_NT_HEADERS *ppe;
	IMAGE_SECTION_HEADER  *psec;
	DWORD i, start, end, raw;

	pmz = (void*)hmodule;
	if( IsBadReadPtr(pmz, sizeof(IMAGE_DOS_HEADER)) || (pmz->e_magic != 0x5A4D) )
		return 0xFFFFFFFF;

	ppe = (void*)((unsigned char*)pmz + pmz->e_lfanew);
	if( IsBadReadPtr(ppe, sizeof(IMAGE_NT_HEADERS)) || (ppe->Signature != 0x4550) )
		return 0xFFFFFFFF;

	if(rva < ppe->OptionalHeader.SizeOfHeaders)
		return rva;

	psec = (void*)((unsigned char*)ppe + sizeof(IMAGE_NT_HEADERS));
	for(i = 0; i < ppe->FileHeader.NumberOfSections; ++i) {
		if(IsBadReadPtr(psec, sizeof(IMAGE_SECTION_HEADER)))
			return 0xFFFFFFFF;
		start = psec->VirtualAddress;
		end = start + psec->Misc.VirtualSize;
		raw = psec->PointerToRawData;

		if( (rva >= start) && (rva < end) )
			return rva + raw - start;
	}

	return 0xFFFFFFFF;
}

// -------------------------------------------------------------------------------------------------

static int restore_code_from_image(HANDLE hmodule, DWORD rva, void *buf, DWORD count)
{
	int success = 0;
	TCHAR path[MAX_PATH];
	HANDLE hfile;
	DWORD pos, dw;

	if((pos = rva_to_file_position(hmodule, rva)) == 0xFFFFFFFF)
		return 0;

	if(!GetModuleFileName(hmodule, path, MAX_PATH))
		return 0;

	hfile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if(hfile != INVALID_HANDLE_VALUE)
	{
		if(SetFilePointer(hfile, pos, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
			ReadFile(hfile, buf, count, &dw, NULL);
			success = (count == dw);
		}
		CloseHandle(hfile);
	}

	return success;
}

// -------------------------------------------------------------------------------------------------

CODE_HOOK_ERROR code_hook_install_dll(code_hook_t *phook, HMODULE hmodule, LPCSTR procname, void *target, int vector, int forced_mode)
{
	CODE_HOOK_ERROR status;
	DWORD rva, pos, fixbytes, protect;
	void *location;
	char original[CODE_HOOK_FORCED_MODE_COMPARESIZE], *changed;

	if((location = GetProcAddress(hmodule, procname)) == NULL)
		return CODE_HOOK_BADPROC;

	// normal mode install
	status = code_hook_install(phook, location, target, vector);

	if( ((status != CODE_HOOK_CONFLICT) && (status != CODE_HOOK_BADCODE)) || (!forced_mode) )
		return status;

	// forced mode install
	if(IsBadReadPtr(location, sizeof(original)))
		return CODE_HOOK_BADREADPTR;

	// read original code
	rva = (DWORD)location - (DWORD)hmodule;
	if(!restore_code_from_image(hmodule, rva, original, sizeof(original)))
		return CODE_HOOK_CANTFIX;

	// count bytes to fix
	fixbytes = 0;
	changed = location;
	for(pos = 0; pos < sizeof(original); ++pos) {
		if(original[pos] != changed[pos])
			fixbytes = pos + 1;
	}
	if( (fixbytes == 0) || (fixbytes > CODE_HOOK_FORCED_MODE_MAX_FIXSIZE) )
		return CODE_HOOK_CANTFIX;

	// fix dll code
	if((protect = set_protection(location, fixbytes, PAGE_EXECUTE_READWRITE)) == 0)
		return CODE_HOOK_CANTPROTECT;
	memcpy(location, original, fixbytes);
	set_protection(location, fixbytes, protect);

	return code_hook_install(phook, location, target, vector);
}

// -------------------------------------------------------------------------------------------------

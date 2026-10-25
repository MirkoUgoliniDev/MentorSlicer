import lldb
import struct

def ReadWideString(process, addr, is_32bit=True):
    error = lldb.SBError()
    # Read reasonable amount
    data = process.ReadMemory(addr, 2048, error)
    if not error.Success():
        return ""
    
    res = ""
    offset = 0
    step = 4 if is_32bit else 2
    
    while offset < len(data):
        chunk = data[offset:offset+step]
        if len(chunk) < step: break
        
        if is_32bit:
            val = struct.unpack('<I', chunk)[0] # Little endian
        else:
            val = struct.unpack('<H', chunk)[0]
            
        if val == 0: break
        try:
            res += chr(val)
        except:
            res += "?"
        offset += step
        
    return res

def StringSummaryProvider(valobj, internal_dict):
    # Try to access _M_dataplus._M_p
    # If fails, try to look at memory manually
    
    # Check if type contains wchar_t
    type_name = valobj.GetType().GetName()
    # Linux wchar_t is usually 32-bit (4 bytes), Windows is 16-bit (2 bytes)
    # We assume Linux 4-byte wchar_t here given the environment.
    is_wide = 'wchar_t' in type_name
    
    # This is a heuristic for GCC's std::string
    # It has _M_dataplus, _M_string_length, etc.
    
    try:
        # If we have debug info, this works
        dataplus = valobj.GetChildMemberWithName('_M_dataplus')
        if dataplus.IsValid():
            mp = dataplus.GetChildMemberWithName('_M_p')
            if mp.IsValid():
                data = mp.GetSummary()
                if data: return data
                # If summary is missing, read memory
                addr = mp.GetValueAsUnsigned(0)
                if addr == 0: return '""'
                
                # Read string from memory
                if is_wide:
                    # Try 32-bit first (Linux standard)
                    s = ReadWideString(valobj.GetProcess(), addr, is_32bit=True)
                    return f'L"{s}"'
                else:
                    error = lldb.SBError()
                    s = valobj.GetProcess().ReadCStringFromMemory(addr, 1024, error)
                    if error.Success():
                        # Heuristic: if C-string is length 1, and we are wxString, it MIGHT be wide string 
                        if len(s) == 1 and 'wxString' in type_name:
                             s_wide = ReadWideString(valobj.GetProcess(), addr, is_32bit=True)
                             if len(s_wide) > 1:
                                  return f'L"{s_wide}"'
                        return f'"{s}"'
    except:
        pass
        
    # Fallback for missing debug info (incomplete type)
    # Assuming standard layout: pointer at offset 0
    try:
        addr = valobj.GetAddress().GetLoadAddress(valobj.GetTarget())
        if addr == lldb.LLDB_INVALID_ADDRESS:
            addr = valobj.GetValueAsUnsigned(0)
            
        # We need to read the pointer at this address.
        # But wait, std::string is a struct. 
        # _M_dataplus is the first member.
        # _M_dataplus has _M_p as first member (usually).
        
        # So dereferencing the address of the string object gives the address of the char buffer.
        error = lldb.SBError()
        pointer_val = valobj.GetProcess().ReadPointerFromMemory(addr, error)
        if error.Success() and pointer_val != 0:
            if is_wide:
                 s = ReadWideString(valobj.GetProcess(), pointer_val, is_32bit=True)
                 return f'L"{s}"'
            else:
                 # Check length? Maybe just read C-string
                 s = valobj.GetProcess().ReadCStringFromMemory(pointer_val, 1024, error)
                 
                 # Heuristic: if C-string is length 1, and we are wxString, it MIGHT be wide string 
                 if error.Success():
                     if len(s) == 1 and 'wxString' in type_name:
                          # Try reading as wide
                          s_wide = ReadWideString(valobj.GetProcess(), pointer_val, is_32bit=True)
                          if len(s_wide) > 1:
                               return f'L"{s_wide}"'
                     return f'"{s}"'
    except:
        pass
        
    return ""

def WxStringSummaryProvider(valobj, internal_dict):
    # wxString is usually a wrapper around std::basic_string or compatible
    # In wx 3.x with std::string enabled, it might inherit from it or have it as member
    
    # Try 1: check if it has m_impl
    try:
        impl = valobj.GetChildMemberWithName('m_impl')
        if impl.IsValid():
            return StringSummaryProvider(impl, internal_dict)
    except:
        pass
        
    # Try 2: treat as std::string directly (inheritance)
    res = StringSummaryProvider(valobj, internal_dict)
    if res and res != "":
        return res
        
    return ""

def WxEventSummaryProvider(valobj, internal_dict):
    # For incomplete wxEvent, we try to resolve the vtable to get dynamic type
    
    try:
        addr = valobj.GetAddress().GetLoadAddress(valobj.GetTarget())
        if addr == lldb.LLDB_INVALID_ADDRESS:
             # Maybe it's a pointer or reference, get its value
             addr = valobj.GetValueAsUnsigned(0)
             
        if addr == 0:
            return "NULL"

        # Read vtable pointer (first word)
        error = lldb.SBError()
        vptr = valobj.GetProcess().ReadPointerFromMemory(addr, error)
        
        if error.Success() and vptr != 0:
            # Resolve symbol for vptr
            addr_obj = valobj.GetTarget().ResolveLoadAddress(vptr)
            sym = addr_obj.GetSymbol()
            if sym.IsValid():
                name = sym.GetName()
                # Name is like "vtable for wxCommandEvent"
                # Demangle usually happens automatically in LLDB API or we can just parse
                if "vtable for " in name:
                    return name.replace("vtable for ", "") + f" @ {hex(addr)}"
                return name + f" @ {hex(addr)}"
    except:
        pass
        
    return f"wxEvent @ {hex(valobj.GetValueAsUnsigned(0))}"

def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('type summary add -F lldb_formatters.StringSummaryProvider "std::string"')
    debugger.HandleCommand('type summary add -F lldb_formatters.StringSummaryProvider "std::__cxx11::string"')
    debugger.HandleCommand('type summary add -F lldb_formatters.StringSummaryProvider "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >"')
    
    debugger.HandleCommand('type summary add -F lldb_formatters.WxStringSummaryProvider "wxString"')
    debugger.HandleCommand('type summary add -F lldb_formatters.WxEventSummaryProvider "wxEvent" -C yes')
    debugger.HandleCommand('type summary add -F lldb_formatters.WxEventSummaryProvider "wxCommandEvent" -C yes')
    debugger.HandleCommand('type summary add -F lldb_formatters.WxEventSummaryProvider "wxNotifyEvent" -C yes')
    debugger.HandleCommand('type summary add -F lldb_formatters.WxEventSummaryProvider "wxMouseEvent" -C yes')
    debugger.HandleCommand('type summary add -F lldb_formatters.WxEventSummaryProvider "wxKeyEvent" -C yes')

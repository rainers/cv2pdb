_TEXT SEGMENT

PUBLIC cvt80to64

cvt80to64 PROC
	fld tbyte ptr [rcx]
	fstp qword ptr [rdx]
	ret 0
cvt80to64 ENDP

_TEXT ENDS
END
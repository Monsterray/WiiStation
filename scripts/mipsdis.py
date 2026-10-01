# mipsdis.py -- a small R3000 disassembler for PS1 RAM dumps (e.g. a save state RAM, see
# .claude/skills/wiistation-diagnostics/references/test-roms.md).
#   python scripts/mipsdis.py RAM.bin OFF:PC:N [OFF:PC:N ...]   (hex file offset, guest PC, word count)
import struct, sys
R=['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7','s0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
def dis(w, pc):
    op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; rd=(w>>11)&31; sh=(w>>6)&31; fn=w&63; imm=w&0xffff; simm=imm-0x10000 if imm&0x8000 else imm
    if w==0: return 'nop'
    if op==0:
        n={0:'sll',2:'srl',3:'sra',4:'sllv',6:'srlv',8:'jr',9:'jalr',12:'syscall',16:'mfhi',18:'mflo',24:'mult',25:'multu',26:'div',27:'divu',32:'add',33:'addu',34:'sub',35:'subu',36:'and',37:'or',38:'xor',39:'nor',42:'slt',43:'sltu'}.get(fn,'sp%d'%fn)
        if fn in (0,2,3): return f'{n} {R[rd]},{R[rt]},{sh}'
        if fn==8: return f'jr {R[rs]}'
        return f'{n} {R[rd]},{R[rs]},{R[rt]}'
    if op in (2,3): return ('j ' if op==2 else 'jal ')+hex(((pc+4)&0xf0000000)|((w&0x3ffffff)<<2))
    if op in (4,5): return ('beq' if op==4 else 'bne')+f' {R[rs]},{R[rt]},{hex(pc+4+simm*4)}'
    if op in (6,7): return ('blez' if op==6 else 'bgtz')+f' {R[rs]},{hex(pc+4+simm*4)}'
    if op==1: return ('bltz' if rt==0 else 'bgez')+f' {R[rs]},{hex(pc+4+simm*4)}'
    n={8:'addi',9:'addiu',10:'slti',11:'sltiu',12:'andi',13:'ori',14:'xori',15:'lui',32:'lb',33:'lh',35:'lw',36:'lbu',37:'lhu',40:'sb',41:'sh',43:'sw',16:'cop0',18:'cop2',50:'lwc2',58:'swc2'}.get(op,'op%d'%op)
    if op==15: return f'lui {R[rt]},{hex(imm)}'
    if op>=32: return f'{n} {R[rt]},{simm}({R[rs]})'
    return f'{n} {R[rt]},{R[rs]},{simm if op in (8,9,10,11) else hex(imm)}'
rom=open(sys.argv[1],'rb').read()
for spec in sys.argv[2:]:
    off,pc,n=[int(x,16) for x in spec.split(':')]
    print('== pc', hex(pc))
    for k in range(n):
        w=struct.unpack('<I', rom[off+4*k:off+4*k+4])[0]; print('  %08x  %08x  %s'%(pc+4*k, w, dis(w, pc+4*k)))

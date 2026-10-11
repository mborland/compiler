//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* The name table forgets a name when an entity is renamed
   (ecil_word_unregister) and finds it again when another entity takes the
   old name; the entry of the old name must be reused, not duplicated, or the
   table (which reinserts entries in order when it grows) loses the name and
   a later reference to it would get a second G-id for the same external
   name.  The renamed function f gives up the name "f"; other takes it; the
   200 functions below force the table to grow; third, declared afterwards
   with the same asm name, must find the G-id of other. */
int use(void) { extern int f(void); return f(); }
int f(void) __asm__("g_name");
int f(void) { return 1; }
int other(void) __asm__("f");
int other(void) { return 2; }
int h0(void);
int h1(void);
int h2(void);
int h3(void);
int h4(void);
int h5(void);
int h6(void);
int h7(void);
int h8(void);
int h9(void);
int h10(void);
int h11(void);
int h12(void);
int h13(void);
int h14(void);
int h15(void);
int h16(void);
int h17(void);
int h18(void);
int h19(void);
int h20(void);
int h21(void);
int h22(void);
int h23(void);
int h24(void);
int h25(void);
int h26(void);
int h27(void);
int h28(void);
int h29(void);
int h30(void);
int h31(void);
int h32(void);
int h33(void);
int h34(void);
int h35(void);
int h36(void);
int h37(void);
int h38(void);
int h39(void);
int h40(void);
int h41(void);
int h42(void);
int h43(void);
int h44(void);
int h45(void);
int h46(void);
int h47(void);
int h48(void);
int h49(void);
int h50(void);
int h51(void);
int h52(void);
int h53(void);
int h54(void);
int h55(void);
int h56(void);
int h57(void);
int h58(void);
int h59(void);
int h60(void);
int h61(void);
int h62(void);
int h63(void);
int h64(void);
int h65(void);
int h66(void);
int h67(void);
int h68(void);
int h69(void);
int h70(void);
int h71(void);
int h72(void);
int h73(void);
int h74(void);
int h75(void);
int h76(void);
int h77(void);
int h78(void);
int h79(void);
int h80(void);
int h81(void);
int h82(void);
int h83(void);
int h84(void);
int h85(void);
int h86(void);
int h87(void);
int h88(void);
int h89(void);
int h90(void);
int h91(void);
int h92(void);
int h93(void);
int h94(void);
int h95(void);
int h96(void);
int h97(void);
int h98(void);
int h99(void);
int h100(void);
int h101(void);
int h102(void);
int h103(void);
int h104(void);
int h105(void);
int h106(void);
int h107(void);
int h108(void);
int h109(void);
int h110(void);
int h111(void);
int h112(void);
int h113(void);
int h114(void);
int h115(void);
int h116(void);
int h117(void);
int h118(void);
int h119(void);
int h120(void);
int h121(void);
int h122(void);
int h123(void);
int h124(void);
int h125(void);
int h126(void);
int h127(void);
int h128(void);
int h129(void);
int h130(void);
int h131(void);
int h132(void);
int h133(void);
int h134(void);
int h135(void);
int h136(void);
int h137(void);
int h138(void);
int h139(void);
int h140(void);
int h141(void);
int h142(void);
int h143(void);
int h144(void);
int h145(void);
int h146(void);
int h147(void);
int h148(void);
int h149(void);
int h150(void);
int h151(void);
int h152(void);
int h153(void);
int h154(void);
int h155(void);
int h156(void);
int h157(void);
int h158(void);
int h159(void);
int h160(void);
int h161(void);
int h162(void);
int h163(void);
int h164(void);
int h165(void);
int h166(void);
int h167(void);
int h168(void);
int h169(void);
int h170(void);
int h171(void);
int h172(void);
int h173(void);
int h174(void);
int h175(void);
int h176(void);
int h177(void);
int h178(void);
int h179(void);
int h180(void);
int h181(void);
int h182(void);
int h183(void);
int h184(void);
int h185(void);
int h186(void);
int h187(void);
int h188(void);
int h189(void);
int h190(void);
int h191(void);
int h192(void);
int h193(void);
int h194(void);
int h195(void);
int h196(void);
int h197(void);
int h198(void);
int h199(void);
int grow(void) {
  return 0 + h0() + h1() + h2() + h3() + h4() + h5() + h6() + h7() + h8() +
  h9() + h10() + h11() + h12() + h13() + h14() + h15() + h16() + h17() + h18()
  + h19() + h20() + h21() + h22() + h23() + h24() + h25() + h26() + h27() +
  h28() + h29() + h30() + h31() + h32() + h33() + h34() + h35() + h36() +
  h37() + h38() + h39() + h40() + h41() + h42() + h43() + h44() + h45() +
  h46() + h47() + h48() + h49() + h50() + h51() + h52() + h53() + h54() +
  h55() + h56() + h57() + h58() + h59() + h60() + h61() + h62() + h63() +
  h64() + h65() + h66() + h67() + h68() + h69() + h70() + h71() + h72() +
  h73() + h74() + h75() + h76() + h77() + h78() + h79() + h80() + h81() +
  h82() + h83() + h84() + h85() + h86() + h87() + h88() + h89() + h90() +
  h91() + h92() + h93() + h94() + h95() + h96() + h97() + h98() + h99() +
  h100() + h101() + h102() + h103() + h104() + h105() + h106() + h107() +
  h108() + h109() + h110() + h111() + h112() + h113() + h114() + h115() +
  h116() + h117() + h118() + h119() + h120() + h121() + h122() + h123() +
  h124() + h125() + h126() + h127() + h128() + h129() + h130() + h131() +
  h132() + h133() + h134() + h135() + h136() + h137() + h138() + h139() +
  h140() + h141() + h142() + h143() + h144() + h145() + h146() + h147() +
  h148() + h149() + h150() + h151() + h152() + h153() + h154() + h155() +
  h156() + h157() + h158() + h159() + h160() + h161() + h162() + h163() +
  h164() + h165() + h166() + h167() + h168() + h169() + h170() + h171() +
  h172() + h173() + h174() + h175() + h176() + h177() + h178() + h179() +
  h180() + h181() + h182() + h183() + h184() + h185() + h186() + h187() +
  h188() + h189() + h190() + h191() + h192() + h193() + h194() + h195() +
  h196() + h197() + h198() + h199();
}
int third(void) __asm__("f");
int after(void) { return third() + other(); }

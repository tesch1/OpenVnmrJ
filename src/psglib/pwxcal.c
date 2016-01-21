// Copyright (C) 2015  University of Oregon
// You may distribute under the terms of either the GNU General Public
// License or the Apache License, as specified in the LICENSE file.
// For more information, see the LICENSE file.
/* 
 */

/*
  Parameters:

        pw = 90 degree excitation pulse on the observe transmitter
        pwx1 = 90 degree excitation pulse on first decoupler
        pwx2 = 90 degree excitation pulse on second decoupler
      jC13 = J(C13-H1) coupling constant
      jN15 = J(N15-H1) coupling constant
      jP31 = J(P31-H1) coupling constant
      jname identifies the nucleus of interest

 */
#include <standard.h>

int    numrfch;

pulsesequence()
{
   double tmpval;
   double pwx1;
   double pwx2;
   double pwx3;
   double pwxlvl1;
   double pwxlvl2;
   double pwxlvl3;
   double jtau;
   char   jname[MAXSTR];
   char   jval[MAXSTR];

 
   pwx1 = getval("pwx1");
   pwx2 = getval("pwx2");
   pwx3 = getval("pwx3");
   pwxlvl1 = getval("pwxlvl1");
   pwxlvl2 = getval("pwxlvl2");
   pwxlvl3 = getval("pwxlvl3");
   getstr("jname",jname);
   strcpy(jval,"j");
   strcat(jval,jname);
   jtau = 1.0 / (2.0 * getval(jval) );


    if ( P_getreal(GLOBAL, "numrfch", &tmpval, 1) < 0 )
       tmpval=0;
    numrfch = (int)(tmpval+0.05);

   mod4(ct, v1);		/*  v1 = 0 1 2 3 */
   dbl(v1, v10);		/* v10 = 0 2 0 2 */
   hlv(ct, v2);
   hlv(v2, v2);
   mod4(v2, v2);		/* v2 = 0 0 0 0 1 1 1 1 2 2 2 2 3 3 3 3 */
   add(v2, v1, v1);
   assign(v2, oph);
   add(v10, oph, oph);

   status(A);
   hsdelay(d1); 
   decpower(pwxlvl1);
   if (numrfch > 2) 
   {  
      dec2power(pwxlvl2);
   }
   if (numrfch > 3)
   {
      dec3power(pwxlvl3);
   }

   status(B);
   rgpulse(pw, v2, rof1, rof2);
   jtau -= pw + rof2;
   delay(jtau - rof1);
   sim4pulse(2*pw, pwx1, pwx2, pwx3, v1, zero, zero, zero, rof1, rof2);
   delay(jtau);
   decpower(dpwr);
   if (numrfch > 2) 
   {
      dec2power(dpwr2);
   }
   if (numrfch > 3)
   {
      dec3power(dpwr3);
   }

   status(C);
}

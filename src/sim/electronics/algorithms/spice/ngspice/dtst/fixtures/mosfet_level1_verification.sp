* MOSFET Level 1 Verification - Simple NMOS I-V Characteristics
* Purpose: Verify MosfetLevel1 device model against ngspice reference
*
* To generate reference data:
*   ngspice -b mosfet_level1_verification.sp -o mosfet_level1_verification.txt
*
* Expected outputs:
*   V(drain) = 5.0V
*   I(VDS)   = ~1.8mA (calculated from Level 1 equations)

.title MOSFET Level 1 I-V Test

* MOSFET: Drain Gate Source Bulk ModelName
M1 drain gate 0 0 NMOS W=10u L=1u

* NMOS Level 1 parameters (simple, well-characterized)
.model NMOS NMOS (
+  LEVEL=1
+  VTO=0.7
+  KP=100u
+  LAMBDA=0.02
+  PHI=0.7
+  GAMMA=0.4
+  TOX=20n
)

* Voltage sources
VDS drain 0 DC 5.0
VGS gate  0 DC 2.0

* DC operating point
.op

* Print node voltages and currents
.print dc V(drain) V(gate) I(VDS)

.control
run
print all
quit
.endc

.end

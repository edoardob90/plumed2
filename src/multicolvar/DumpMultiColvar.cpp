/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2014-2019 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "core/ActionAtomistic.h"
#include "core/ActionPilot.h"
#include "core/ActionRegister.h"
#include "tools/Pbc.h"
#include "tools/File.h"
#include "core/PlumedMain.h"
#include "core/Atoms.h"
#include "tools/Units.h"
#include <cstdio>
#include "core/SetupMolInfo.h"
#include "core/ActionSet.h"
#include "MultiColvarBase.h"
#include "vesselbase/ActionWithInputVessel.h"
#include "vesselbase/StoreDataVessel.h"

using namespace std;

namespace PLMD
{
namespace multicolvar {

//+PLUMEDOC PRINTANALYSIS DUMPMULTICOLVAR
/*
Dump atom positions and multicolvar on a file.

\par Examples

In this examples we calculate the distances between the  atoms of the first and the second
group and we write them in the file MULTICOLVAR.xyz. For each couple it writes the
coordinates of their geometric center and their distance.

\plumedfile
pos:   GROUP ATOMS=220,221,235,236,247,248,438,439,450,451,534,535
neg:   GROUP ATOMS=65,68,138,182,185,267,270,291,313,316,489,583,621,711
DISTANCES GROUPA=pos GROUPB=neg LABEL=slt

DUMPMULTICOLVAR DATA=slt FILE=MULTICOLVAR.xyz
\endplumedfile

(see also \ref DISTANCES)

*/
//+ENDPLUMEDOC

class DumpMultiColvar:
  public ActionPilot,
  public ActionAtomistic,
  public vesselbase::ActionWithInputVessel
{
  OFile of;
  double lenunit;
  MultiColvarBase* mycolv;
  std::string fmt_xyz, box_string, file_type;
  std::vector<std::string> names;
  std::vector<unsigned>    residueNumbers;
  std::vector<std::string> residueNames;
public:
  explicit DumpMultiColvar(const ActionOptions&);
  ~DumpMultiColvar();
  static void registerKeywords( Keywords& keys );
  void calculate() override {}
  void calculateNumericalDerivatives( ActionWithValue* vv ) override { plumed_error(); }
  void apply() override {}
  void update() override;
};

PLUMED_REGISTER_ACTION(DumpMultiColvar,"DUMPMULTICOLVAR")

void DumpMultiColvar::registerKeywords( Keywords& keys ) {
  Action::registerKeywords( keys );
  ActionAtomistic::registerKeywords( keys );
  ActionPilot::registerKeywords( keys );
  ActionWithInputVessel::registerKeywords( keys );
  keys.add("compulsory","STRIDE","1","the frequency with which the atoms should be output");
  keys.add("compulsory", "FILE", "file on which to output coordinates");
  keys.add("compulsory", "UNITS","PLUMED","the units in which to print out the coordinates. PLUMED means internal PLUMED units");
  keys.add("optional","PRECISION","The number of digits in trajectory file");
  keys.add("atoms","ORIGIN","You can use this keyword to specify the position of an atom as an origin. The positions output will then be displayed relative to that origin");
}

DumpMultiColvar::DumpMultiColvar(const ActionOptions&ao):
  Action(ao),
  ActionPilot(ao),
  ActionAtomistic(ao),
  ActionWithInputVessel(ao)
{
  readArgument("store");
  mycolv = dynamic_cast<MultiColvarBase*>( getDependencies()[0] );
  plumed_assert( getDependencies().size()==1 );
  if(!mycolv) error("action labeled " + mycolv->getLabel() + " is not a multicolvar");
  log.printf("  printing colvars calculated by action %s \n",mycolv->getLabel().c_str() );

  std::vector<AtomNumber> atom;
  parseAtomList("ORIGIN",atom);
  if( atom.size()>1 ) error("should only be one atom specified");
  if( atom.size()==1 ) log.printf("  origin is at position of atom : %d\n",atom[0].serial() );

  string file; parse("FILE",file);
  if(file.length()==0) error("name out output file was not specified");
  file_type=Tools::extension(file);
  log<<"  file name "<<file<<"\n";
/* Allow for printing colvars in Extended XYZ file */
  if(file_type!="xyz" && file_type!="extxyz") error("can only print XYZ or extended XYZ file type with DUMPMULTICOLVAR");

  fmt_xyz="%f";

  string precision; parse("PRECISION",precision);
  if(precision.length()>0) {
    int p; Tools::convert(precision,p);
    log<<"  with precision "<<p<<"\n";
    string a,b;
    Tools::convert(p+5,a);
    Tools::convert(p,b);
    fmt_xyz="%"+a+"."+b+"f";
  }

  std::string unitname; parse("UNITS",unitname);
  if(unitname!="PLUMED") {
    Units myunit; myunit.setLength(unitname);
    lenunit=plumed.getAtoms().getUnits().getLength()/myunit.getLength();
  }
  else lenunit=1.0;

  checkRead();
  of.link(*this);
  of.open(file);
  log.printf("  printing atom positions in %s units \n", unitname.c_str() );
  requestAtoms(atom); addDependency( mycolv );

  // Check if molinfo data are supplied to have proper atom names
  std::vector<SetupMolInfo*> moldat=plumed.getActionSet().select<SetupMolInfo*>();
  if (moldat.size()==1) {
      log.printf("  MOLINFO data found. Using proper atom names\n");
      names.resize(atom.size());
      for (unsigned i=0; i<atom.size(); ++i) names[i]=moldat[0]->getAtomName( mycolv->getAbsoluteIndexOfCentralAtom(i) );
  } else log.printf("  No MOLINFO data found\n");
}

void DumpMultiColvar::update() {
  of.printf("%u\n",mycolv->getCurrentNumberOfActiveTasks());
  const Tensor & t(mycolv->getPbc().getBox());
  if(mycolv->getPbc().isOrthorombic()) {
    if (file_type == "xyz") { 
        box_string = " "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+"\n";
        of.printf(box_string.c_str(),lenunit*t(0,0),lenunit*t(1,1),lenunit*t(2,2));
    } else {
        box_string = "Lattice=\""+fmt_xyz+" 0.0 0.0 0.0 "+fmt_xyz+" 0.0 0.0 0.0 "+fmt_xyz+"\" Properties=species:S:1:pos:R:3:%s:R:1\n";
        of.printf(box_string.c_str(), lenunit*t(0,0),lenunit*t(1,1),lenunit*t(2,2),
                                      mycolv->getLabel().c_str()
                  );
    }
  } else {
    if (file_type == "xyz") box_string = " "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+"\n";
    else box_string = "Lattice=\""+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz+"\" Properties=species:S:1:pos:R:3:%s:R:1\n";
    of.printf(box_string.c_str(),
              lenunit*t(0,0),lenunit*t(0,1),lenunit*t(0,2),
              lenunit*t(1,0),lenunit*t(1,1),lenunit*t(1,2),
              lenunit*t(2,0),lenunit*t(2,1),lenunit*t(2,2),
              mycolv->getLabel().c_str()
             );
  }
  vesselbase::StoreDataVessel* stash=dynamic_cast<vesselbase::StoreDataVessel*>( getPntrToArgument() );
  plumed_dbg_assert( stash );
  std::vector<double> cvals( mycolv->getNumberOfQuantities() );
  for(unsigned i=0; i<mycolv->getCurrentNumberOfActiveTasks(); ++i) {
    const char* defname="X";
    const char* name=defname;
    if (names.size()>0) if (names[i].length()>0) name=names[i].c_str();

    Vector apos = mycolv->getCentralAtomPos( mycolv->getPositionInFullTaskList(i) );
    if( getNumberOfAtoms()>0 ) apos=pbcDistance( getPosition(0), apos );
    of.printf(("%s "+fmt_xyz+" "+fmt_xyz+" "+fmt_xyz).c_str(),name,lenunit*apos[0],lenunit*apos[1],lenunit*apos[2]);
    stash->retrieveSequentialValue( i, true, cvals );
    if( mycolv->weightWithDerivatives() ) {
      for(unsigned j=0; j<cvals.size(); ++j) of.printf((" "+fmt_xyz).c_str(),cvals[j]);
    } else {
      for(unsigned j=1; j<cvals.size(); ++j) of.printf((" "+fmt_xyz).c_str(),cvals[j]);
    }
    of.printf("\n");
  }
}

DumpMultiColvar::~DumpMultiColvar() {
}


}
}

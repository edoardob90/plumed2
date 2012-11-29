/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2012 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.0.

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
#include "Pbc.h"
#include "Tools.h"
#include "PlumedException.h"

using namespace PLMD;

Pbc::Pbc():
  type(unset)
{
  box.zero();
  invBox.zero();
}


void Pbc::setBox(const Tensor&b){
  box=b;
// UP TO NOW ONLY WORKS WITH ORTHOROMIBIC (should implement matrix inversion)
// detect type:
  const double epsilon=1e-28;

  type=unset;
  double det=box.determinant();
  if(det*det<epsilon)return;

  bool cxy=false;
  bool cxz=false;
  bool cyz=false;
  if(box(0,1)*box(0,1)<epsilon && box(1,0)*box(1,0)<epsilon) cxy=true;
  if(box(0,2)*box(0,2)<epsilon && box(2,0)*box(2,0)<epsilon) cxz=true;
  if(box(1,2)*box(1,2)<epsilon && box(2,1)*box(2,1)<epsilon) cyz=true;

  invBox=box.inverse();

  if(cxy && cxz && cyz) type=orthorombic;
// NOT IMPLEMENTED YET; WILL FALL TO GENERIC
//  else if(cxy && cxz)   type=yz;
//  else if(cxy && cyz)   type=xz;
//  else if(cxz && cyz)   type=xy;
  else type=generic;
}

double Pbc::distance( const bool pbc, const Vector& v1, const Vector& v2 ) const {
  if(pbc){ return ( distance(v1,v2) ).modulo(); }
  else{ return ( delta(v1,v2) ).modulo(); }
}

Vector Pbc::distance(const Vector&v1,const Vector&v2)const{
  Vector d=delta(v1,v2);
  if(type==unset){
  } else if(type==orthorombic) {
    for(int i=0;i<3;i++) d[i]=Tools::pbc(d[i]*invBox(i,i))*box(i,i);
  } else if(type==generic) {
// first reduce:
   Vector s=matmul(invBox.transpose(),d);
   for(int i=0;i<3;i++) s[i]=Tools::pbc(s[i]);
   d=matmul(box.transpose(),s);
// then check all the neighbors:
    Vector a1(box(0,0),box(0,1),box(0,2));
    Vector a2(box(1,0),box(1,1),box(1,2));
    Vector a3(box(2,0),box(2,1),box(2,2));
    Vector best(d);
    double lbest=best.modulo2();
    Vector trial;
    double ltrial;
    for(int i=-1;i<=1;i++) for(int j=-1;j<=1;j++) for(int k=-1;k<=1;k++) {
      trial=d+i*a1+j*a2+k*a3;
      ltrial=trial.modulo2();
      if(ltrial<lbest){
        best=trial;
        lbest=ltrial;
      }
    }
    d=best;
  } else plumed_merror("unknown pbc type");
  return d;
}

Vector Pbc::realToScaled(const Vector&d)const{
  return matmul(invBox.transpose(),d);
}

Vector Pbc::scaledToReal(const Vector&d)const{
  return matmul(box.transpose(),d);
}

bool Pbc::isOrthorombic()const{
  return type==orthorombic;
}

const Tensor& Pbc::getBox()const{
  return box;
}

const Tensor& Pbc::getInvBox()const{
  return invBox;
}




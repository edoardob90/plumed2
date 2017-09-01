/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2016,2017 The plumed team
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
#include <iostream>
#include <complex>
#include "ActionWithInputGrid.h"
#include "core/ActionRegister.h"
#ifdef __PLUMED_HAS_FFTW
#include "fftw3.h" // FFTW interface
#endif

namespace PLMD {
namespace gridtools {

//+PLUMEDOC GRIDANALYSIS FOURIER_TRANSFORM
/*
Compute the Discrete Fourier Transform (DFT) by means of FFTW of data stored on a 2D grid.

This action can operate on any other action that outputs scalar data on a two-dimensional grid.

Up to now, even if the input data are purely real the action uses a complex DFT.

Just as a quick reference, given a 1D array \f$\mathbf{X}\f$ of size \f$n\f$, this action computes the vector \f$\mathbf{Y}\f$ given by

\f[
Y_k = \sum_{j=0}^{n-1} X_j e^{2\pi\, j k \sqrt{-1}/n}.
\f]

This can be easily extended to more than one dimension. All the other details can be found at http://www.fftw.org/doc/What-FFTW-Really-Computes.html#What-FFTW-Really-Computes.

The keyword "FOURIER_PARAMETERS" deserves just a note on the usage. This keyword specifies how the Fourier transform will be normalized. The keyword takes two numerical parameters (\f$a,\,b\f$) that define the normalization according to the following expression

\f[
\frac{1}{n^{(1-a)/2}} \sum_{j=0}^{n-1} X_j e^{2\pi b\, j k \sqrt{-1}/n}
\f]

The default values of these parameters are: \f$a=1\f$ and \f$b=1\f$.

\par Examples

The following example tells Plumed to compute the complex 2D 'backward' Discrete Fourier Transform by taking the data saved on a grid called 'density', and normalizing the output by \f$ \frac{1}{\sqrt{N_x\, N_y}}\f$, where \f$N_x\f$ and \f$N_y\f$ are the number of data on the grid (it can be the case that \f$N_x \neq N_y\f$):

\plumedfile
FOURIER_TRANSFORM STRIDE=1 GRID=density FT_TYPE=complex FOURIER_PARAMETERS=0,-1 FILE=fourier.dat
\endplumedfile

*/
//+ENDPLUMEDOC


class FourierTransform : public ActionWithInputGrid {
private:
  std::string output_type;
  bool real_output, store_norm;
  std::vector<unsigned> gdirs;
  std::vector<int> fourier_params;
public:
  static void registerKeywords( Keywords& keys );
  explicit FourierTransform(const ActionOptions&ao);
#ifndef __PLUMED_HAS_FFTW
  void performOperations( const bool& from_update ) {}
#else
  void performOperations( const bool& from_update );
#endif
  void compute( const unsigned&, MultiValue& ) const {}
  bool isPeriodic() { return false; }
};

PLUMED_REGISTER_ACTION(FourierTransform,"FOURIER_TRANSFORM")

void FourierTransform::registerKeywords( Keywords& keys ) {
  ActionWithInputGrid::registerKeywords( keys ); keys.remove("BANDWIDTH"); keys.remove("KERNEL");
  keys.add("optional","FT_TYPE","choose what kind of data you want as output on the grid. Possible values are: ABS = compute the complex modulus of Fourier coefficients (DEFAULT); NORM = compute the norm (i.e. ABS^2) of Fourier coefficients; COMPLEX = store the FFTW complex output on the grid (as a vector).");
  keys.add("compulsory","FOURIER_PARAMETERS","default","what kind of normalization is applied to the output and if the Fourier transform in FORWARD or BACKWARD. This keyword takes the form FOURIER_PARAMETERS=A,B, where A and B can be 0, 1 or -1. The default values are A=1 (no normalization at all) and B=1 (forward FFT). Other possible choices for A are: "
           "A=-1: normalize by the number of data, "
           "A=0: normalize by the square root of the number of data (one forward and followed by backward FFT recover the original data). ");
}

FourierTransform::FourierTransform(const ActionOptions&ao):
  Action(ao),
  ActionWithInputGrid(ao),
  real_output(true),
  store_norm(false),
  fourier_params(2)
{
#ifndef __PLUMED_HAS_FFTW
  error("this feature is only available if you compile PLUMED with FFTW");
#else
  // Get the type of FT
  parse("FT_TYPE",output_type);
  if (output_type.length()==0) {
    log<<"  keyword FT_TYPE unset. By default output grid will contain REAL Fourier coefficients\n";
  } else if ( output_type=="ABS" || output_type=="abs") {
    log << "  keyword FT_TYPE is '"<< output_type << "' : will compute the MODULUS of Fourier coefficients\n";
  } else if ( output_type=="NORM" || output_type=="norm") {
    log << "  keyword FT_TYPE is '"<< output_type << "' : will compute the NORM of Fourier coefficients\n";
    store_norm=true;
  } else if ( output_type=="COMPLEX" || output_type=="complex" ) {
    log<<"  keyword FT_TYPE is '"<< output_type <<"' : output grid will contain the COMPLEX Fourier coefficients\n";
    real_output=false;
  } else error("keyword FT_TYPE unrecognized!");

  // Normalize output?
  std::string params_str; parse("FOURIER_PARAMETERS",params_str);
  if (params_str=="default") {
    fourier_params.assign( fourier_params.size(), 1 );
    log.printf("  default values of Fourier parameters A=%i, B=%i : the output will NOT be normalized and BACKWARD Fourier transform is computed \n", fourier_params[0],fourier_params[1]);
  } else {
    std::vector<std::string> fourier_str = Tools::getWords(params_str, "\t\n ,");
    if (fourier_str.size()>2) error("FOURIER_PARAMETERS can take just two values");
    for (unsigned i=0; i<fourier_str.size(); ++i) {
      Tools::convert(fourier_str[i],fourier_params[i]);
      if (fourier_params[i]>1 || fourier_params[i]<-1) error("values accepted for FOURIER_PARAMETERS are only -1, 1 or 0");
    }
    log.printf("  Fourier parameters are A=%i, B=%i \n", fourier_params[0],fourier_params[1]);
  }


  // Create the input from the old string
  std::string vstring;
  unsigned n=0; gdirs.resize( ingrid->getDimension() );
  for(unsigned i=0; i<ingrid->getDimension(); ++i) {
    gdirs[n]=i; n++;
  }

  plumed_assert( n==ingrid->getDimension() );

  if (real_output) {
    if (!store_norm) vstring="COMPONENTS=" + getLabel() + "_abs";
    else vstring="COMPONENTS=" + getLabel() + "_norm";
  } else vstring="COMPONENTS=" + getLabel() + "_real," + getLabel() + "_imag";

  // Set COORDINATES keyword
  vstring += " COORDINATES=" + ingrid->getComponentName( gdirs[0] );
  for(unsigned i=1; i<gdirs.size(); ++i) vstring += "," + ingrid->getComponentName( gdirs[i] );

  // Set PBC keyword
  vstring += " PBC=";
  if( ingrid->isPeriodic(gdirs[0]) ) vstring+="T"; else vstring+="F";
  for(unsigned i=1; i<gdirs.size(); ++i) {
    if( ingrid->isPeriodic(gdirs[i]) ) vstring+=",T"; else vstring+=",F";
  }


  // Create a grid on which to store the fourier transform of the input grid
  createGrid( "grid", vstring );
  if( ingrid->noDerivatives() ) mygrid->setNoDerivatives();
  setAveragingAction( mygrid, false );

  checkRead();
#endif
}

#ifdef __PLUMED_HAS_FFTW
void FourierTransform::performOperations( const bool& from_update ) {
  

  // Spacing of the real grid
  std::vector<double> g_spacing ( ingrid->getGridSpacing() );
  // Spacing of the k-grid
  std::vector<double> ft_spacing;
  // Extents of the k-grid
  std::vector<std::string> ft_min( ingrid->getMin() ), ft_max( ingrid->getMax() );
  // Number of bins in the k-grid (equal to the number of bins in the real grid)
  std::vector<unsigned> ft_bins ( ingrid->getNbin() );
  
  log.printf("  === FFTW setup === \n");
  log.printf("   Rank : %d \n   Dimensions :\n", ingrid->getDimension());
  for (unsigned i=0; i<ingrid->getDimension(); ++i) {
    // Check PBC in current grid dimension
    if( !ingrid->isPeriodic(i) ) ft_bins[i]++;
    // Compute k-grid extents
    double dmin, dmax;
    Tools::convert(ft_min[i],dmin); Tools::convert(ft_max[i],dmax);
    // We want to have the min of k-grid at point (0,0)
    dmin=0.0;
    dmax=2.0*pi*ft_bins[i]/( ingrid->getGridExtent(i) );
    Tools::convert(dmin,ft_min[i]); Tools::convert(dmax,ft_max[i]);
    // Info on FFT grid
    i==0 ? log.printf("   %d\n", ingrid->getNbin()[i]) : log.printf("    x %d \n", ingrid->getNbin()[i]);
  }

  // This is the actual setup of the k-grid
  mygrid->setBounds( ft_min, ft_max, ft_bins, ft_spacing); resizeFunctions();
  
  // Get the size of the input data arrays (to allocate FFT data)
  //std::vector<unsigned> input_grid_bins( ingrid->getNbin() );
  //size_t fft_dimension=1; for(unsigned i=0; i<input_grid_bins.size(); ++i) fft_dimension*=static_cast<size_t>( input_grid_bins[i] );
  int *input_grid_bins = (int*)malloc( ingrid->getDimension()*sizeof(int) );
  size_t fft_dimension=1; double norm=1.0;
  for (unsigned i=0; i<ingrid->getDimension(); ++i){
      input_grid_bins[i]=ingrid->getNbin()[i];
      fft_dimension*=input_grid_bins[i];
      // Compute the normalization constant
      norm*=pow( input_grid_bins[i], (1-fourier_params[0])/2 );
  }
  
  // FFT arrays
  //std::vector<std::complex<double> > input_data(fft_dimension), fft_data(fft_dimension);
  // Do the allocation according to FFTW manual
  fftw_complex *input_data, *fft_data;
  input_data = fftw_alloc_complex(fft_dimension); fft_data = fftw_alloc_complex(fft_dimension);

  // Fill real input with the data on the grid
  std::vector<unsigned> ind( ingrid->getDimension() );
  for (unsigned i=0; i<ingrid->getNumberOfPoints(); ++i) {
    // Get point indices
    ingrid->getIndices(i, ind);
    // Fill input data in row-major order
    plumed_dbg_assert( ind.size()==2 && ind[0]*input_grid_bins[0]+ind[1]<input_data.size() );
    input_data[i][0] = getFunctionValue(i); input_data[i][1] = 0.0;
    //input_data[i].real( getFunctionValue(i) ); input_data[i].imag(0);
    // !!! 2D version; won't work in 1D
    // input_data[ind[0]*input_grid_bins[0]+ind[1]].real( getFunctionValue( i ) );
    // input_data[ind[0]*input_grid_bins[0]+ind[1]].imag( 0.0 );
  }

  //fftw_plan plan_complex = fftw_plan_dft_2d(N_input_data[0], N_input_data[1], reinterpret_cast<fftw_complex*>(&input_data[0]), reinterpret_cast<fftw_complex*>(&fft_data[0]), fourier_params[1], FFTW_ESTIMATE);

  // General FFTW plan (arbitrary dimensions)
  fftw_plan plan_complex = fftw_plan_dft( ingrid->getDimension(), input_grid_bins, input_data, fft_data, fourier_params[1], FFTW_ESTIMATE );
  
  // Compute FT
  fftw_execute( plan_complex );
  
  // Compute the normalization constant
  //double norm=1.0;
  //for (unsigned i=0; i<input_grid_bins.size(); ++i) {
  //  norm *= pow( input_grid_bins[i], (1-fourier_params[0])/2 );
  //}


  // Save FT data to output grid
  std::vector<unsigned> output_grid_bins( mygrid->getNbin() );
  std::vector<unsigned> out_ind ( mygrid->getDimension() );
  for(unsigned i=0; i<mygrid->getNumberOfPoints(); ++i) {
    mygrid->getIndices( i, out_ind );
    if (real_output) {
      double ft_value;
      // Compute abs/norm and fix normalization
      if (!store_norm) ft_value=std::abs( ( pow(fft_data[i][0],2)*pow(fft_data[i][1],2) )/norm ); //ft_value=std::abs( fft_data[out_ind[0]*output_grid_bins[0]+out_ind[1]] / norm );
      else ft_value=sqrt( ( pow(fft_data[i][0],2)*pow(fft_data[i][1],2) )/norm ); //ft_value=std::norm( fft_data[out_ind[0]*output_grid_bins[0]+out_ind[1]] / norm );
      // Save value on the output grid
      mygrid->setGridElement( i, 0, ft_value );
    } else {
      double ft_value_real, ft_value_imag;
      ft_value_real=fft_data[i][0] / norm; //ft_value_real=fft_data[out_ind[0]*output_grid_bins[0]+out_ind[1]].real() / norm;
      ft_value_imag=fft_data[i][1] / norm; //ft_value_imag=fft_data[out_ind[0]*output_grid_bins[0]+out_ind[1]].imag() / norm;
      // Save values on the output grid
      mygrid->setGridElement( i, 0, ft_value_real);
      mygrid->setGridElement( i, 1, ft_value_imag);
    }
  }

  // Free FFTW stuff
  fftw_destroy_plan(plan_complex);
  fftw_cleanup();

}
#endif

} // end namespace 'gridtools'
} // end namespace 'PLMD'

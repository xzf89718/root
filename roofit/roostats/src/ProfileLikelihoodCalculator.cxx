// @(#)root/roostats:$Id$
// Author: Kyle Cranmer   28/07/2008

/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//_________________________________________________
/*
BEGIN_HTML
<p>
ProfileLikelihoodCalculator is a concrete implementation of CombinedCalculator 
(the interface class for a tools which can produce both RooStats HypoTestResults and ConfIntervals).  
The tool uses the profile likelihood ratio as a test statistic, and assumes that Wilks' theorem is valid.  
Wilks' theorem states that -2* log (profile likelihood ratio) is asymptotically distributed as a chi^2 distribution 
with N-dof, where N is the number of degrees of freedom.  Thus, p-values can be constructed and the profile likelihood ratio
can be used to construct a LikelihoodInterval.
(In the future, this class could be extended to use toy Monte Carlo to calibrate the distribution of the test statistic).
</p>
<p> Usage: It uses the interface of the CombinedCalculator, so that it can be configured by specifying:
<ul>
 <li>a model common model (eg. a family of specific models which includes both the null and alternate),</li>
 <li>a data set, </li>
 <li>a set of parameters of which specify the null (including values and const/non-const status), </li>
 <li>a set of parameters of which specify the alternate (including values and const/non-const status),</li>
 <li>a set of parameters of nuisance parameters  (including values and const/non-const status).</li>
</ul>
The interface allows one to pass the model, data, and parameters via a workspace and then specify them with names.
The interface will be extended so that one does not need to use a workspace.
</p>
<p>
After configuring the calculator, one only needs to ask GetHypoTest() (which will return a HypoTestResult pointer) or GetInterval() (which will return an ConfInterval pointer).
</p>
<p>
The concrete implementations of this interface should deal with the details of how the nuisance parameters are
dealt with (eg. integration vs. profiling) and which test-statistic is used (perhaps this should be added to the interface).
</p>
<p>
The motivation for this interface is that we hope to be able to specify the problem in a common way for several concrete calculators.
</p>
END_HTML
*/
//

#ifndef RooStats_ProfileLikelihoodCalculator
#include "RooStats/ProfileLikelihoodCalculator.h"
#endif

#ifndef RooStats_RooStatsUtils
#include "RooStats/RooStatsUtils.h"
#endif

#include "RooStats/LikelihoodInterval.h"
#include "RooStats/HypoTestResult.h"

#include "RooFitResult.h"
#include "RooRealVar.h"
#include "RooProfileLL.h"
#include "RooNLLVar.h"
#include "RooGlobalFunc.h"
#include "RooProdPdf.h"

ClassImp(RooStats::ProfileLikelihoodCalculator) ;

using namespace RooFit;
using namespace RooStats;


//_______________________________________________________
ProfileLikelihoodCalculator::ProfileLikelihoodCalculator() : 
   CombinedCalculator(), fFitResult(0)
{
   // default constructor
}

ProfileLikelihoodCalculator::ProfileLikelihoodCalculator(RooAbsData& data, RooAbsPdf& pdf, const RooArgSet& paramsOfInterest, 
                                                         Double_t size, const RooArgSet* nullParams ) :
   CombinedCalculator(data,pdf, paramsOfInterest, size, nullParams ), 
   fFitResult(0)
{
   // constructor from pdf and parameters
   // the pdf must contain eventually the nuisance parameters
}

ProfileLikelihoodCalculator::ProfileLikelihoodCalculator(RooAbsData& data,  ModelConfig& model, Double_t size) :
   CombinedCalculator(data, model, size), 
   fFitResult(0)
{
   assert(model.GetPdf() );
   // construct from model config (pdf from the model config does not include the nuisance)
   if (model.GetPriorPdf() ) { 
      std::string name = std::string("Costrained_") + (model.GetPdf())->GetName() + std::string("_with_") + (model.GetPriorPdf())->GetName();
      fPdf = new RooProdPdf(name.c_str(),name.c_str(), *(model.GetPdf()), *(model.GetPriorPdf()) );
      // set pdf in ModelConfig which will import in WS  that will manage it 
      model.SetPdf(*fPdf);
   }
}


//_______________________________________________________
ProfileLikelihoodCalculator::~ProfileLikelihoodCalculator(){
   // destructor
   // cannot delete prod pdf because it will delete all the composing pdf's
//    if (fOwnPdf) delete fPdf; 
//    fPdf = 0; 
   if (fFitResult) delete fFitResult; 
}

void ProfileLikelihoodCalculator::DoReset() const { 
   // reset and clear fit result 
   // to be called when a new model or data are set in the calculator 
   if (fFitResult) delete fFitResult; 
   fFitResult = 0; 
}

void  ProfileLikelihoodCalculator::DoGlobalFit() const { 
   // perform a global fit of the likelihood letting with all parameter of interest and 
   // nuisance parameters 
   // keep the list of fitted parameters 

   DoReset(); 
   RooAbsPdf * pdf = GetPdf();
   RooAbsData* data = GetData(); 
   if (!data || !pdf ) return;

   // get all non-const parameters
   RooArgSet* constrainedParams = pdf->getParameters(*data);
   if (!constrainedParams) return ; 
   RemoveConstantParameters(constrainedParams);

   // calculate MLE 
   RooFitResult* fit = pdf->fitTo(*data, Constrain(*constrainedParams),Strategy(1),Hesse(kTRUE),Save(kTRUE),PrintLevel(-1));
  
   // for debug 
   fit->Print();

   delete constrainedParams; 
   // store fit result for further use 
   fFitResult =  fit; 
}

//_______________________________________________________
LikelihoodInterval* ProfileLikelihoodCalculator::GetInterval() const {
   // Main interface to get a RooStats::ConfInterval.  
   // It constructs a profile likelihood ratio and uses that to construct a RooStats::LikelihoodInterval.

//    RooAbsPdf* pdf   = fWS->pdf(fPdfName);
//    RooAbsData* data = fWS->data(fDataName);
   RooAbsPdf * pdf = GetPdf();
   RooAbsData* data = GetData(); 
   if (!data || !pdf || !fPOI) return 0;

   RooArgSet* constrainedParams = pdf->getParameters(*data);
   RemoveConstantParameters(constrainedParams);


   /*
   RooNLLVar* nll = new RooNLLVar("nll","",*pdf,*data, Extended(),Constrain(*constrainedParams));
   RooProfileLL* profile = new RooProfileLL("pll","",*nll, *fPOI);
   profile->addOwnedComponents(*nll) ;  // to avoid memory leak
   */

   RooAbsReal* nll = pdf->createNLL(*data, CloneData(kTRUE), Constrain(*constrainedParams));
   RooAbsReal* profile = nll->createProfile(*fPOI);
   profile->addOwnedComponents(*nll) ;  // to avoid memory leak


   //RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL) ;
   // perform a Best Fit 
   if (!fFitResult) DoGlobalFit();
   // if fit fails return
   if (!fFitResult) return 0;

   // t.b.f. " RooProfileLL should keep and prvide possibility to query on global minimum
   // set POI to fit value (this will speed up profileLL calcualtion of global minimum)
   const RooArgList & fitParams = fFitResult->floatParsFinal(); 
   for (int i = 0; i < fitParams.getSize(); ++i) {
      RooRealVar & fitPar =  (RooRealVar &) fitParams[i];
      RooRealVar * par = (RooRealVar*) fPOI->find( fitPar.GetName() );      
      if (par) { 
         par->setVal( fitPar.getVal() );
         par->setError( fitPar.getError() );
      }
   }
  
   profile->getVal(); // do this so profile will cache the minimum
   //RooMsgService::instance().setGlobalKillBelow(RooFit::DEBUG) ;
   profile->Print();

   TString name = TString("LikelihoodInterval_") + TString(GetName() ); 
   // make a list of fPOI with fit result values 
   TIter iter = fPOI->createIterator(); 
   RooArgSet fitParSet(fitParams); 
   RooArgSet bestPOI; 
   while (RooAbsArg * arg =  (RooAbsArg*) iter.Next() ) { 
      RooAbsArg * p  =  fitParSet.find( arg->GetName() );
      if (p) bestPOI.add(*p);
      else bestPOI.add(*arg);
   }
   LikelihoodInterval* interval = new LikelihoodInterval(name, profile, &bestPOI);
   interval->SetConfidenceLevel(1.-fSize);
   delete constrainedParams;
   return interval;
}

//_______________________________________________________
HypoTestResult* ProfileLikelihoodCalculator::GetHypoTest() const {
   // Main interface to get a HypoTestResult.
   // It does two fits:
   // the first lets the null parameters float, so it's a maximum likelihood estimate
   // the second is to the null (fixing null parameters to their specified values): eg. a conditional maximum likelihood
   // the ratio of the likelihood at the conditional MLE to the MLE is the profile likelihood ratio.
   // Wilks' theorem is used to get p-values 

//    RooAbsPdf* pdf   = fWS->pdf(fPdfName);
//    RooAbsData* data = fWS->data(fDataName);
   RooAbsPdf * pdf = GetPdf();
   RooAbsData* data = GetData(); 

   if (!data || !pdf) return 0;

   if (!fNullParams) return 0; 

   // do a global fit 
   if (!fFitResult) DoGlobalFit(); 
   if (!fFitResult) return 0; 

   RooArgSet* constrainedParams = pdf->getParameters(*data);
   RemoveConstantParameters(constrainedParams);

   // perform a global fit if it is not done before
   if (!fFitResult) DoGlobalFit(); 
   Double_t NLLatMLE= fFitResult->minNll();



   // set POI to given values, set constant, calculate conditional MLE
   RooArgList poiList; /// make ordered list since a vector will be associated to keep parameter values
   poiList.add(*fNullParams); 
   std::vector<double> oldValues(poiList.getSize() ); 
   for (unsigned int i = 0; i < oldValues.size(); ++i) { 
      RooRealVar * mytarget = (RooRealVar*) constrainedParams->find(poiList[i].GetName());
      if (mytarget) { 
         oldValues[i] = mytarget->getVal(); 
         mytarget->setVal( ( (RooRealVar&) poiList[i] ).getVal() );
         mytarget->setConstant(kTRUE);
      }
   }

   

   // perform the fit only if nuisance parameters are available
   // get nuisance parameters
   // nuisance parameters are the non const parameters from the likelihood parameters
   RooArgSet nuisParams(*constrainedParams);

   // need to remove the parameter of interest
   RemoveConstantParameters(&nuisParams);

   // check there are variable parameter in order to do a fit 
   bool existVarParams = false; 
   TIter it = nuisParams.createIterator();
   RooRealVar * myarg = 0; 
   while ((myarg = (RooRealVar *)it.Next())) { 
      if ( !myarg->isConstant() ) {
         existVarParams = true; 
         break;
      }
   }

   Double_t NLLatCondMLE = NLLatMLE; 
   if (existVarParams) {

      RooFitResult* fit2 = pdf->fitTo(*data,Constrain(*constrainedParams),Hesse(kFALSE),Strategy(0), Minos(kFALSE), Save(kTRUE),PrintLevel(-1));
     
      NLLatCondMLE = fit2->minNll();
      fit2->Print();
   }
   else { 
      // get just the likelihood value (no need to do a fit since the likelihood is a constant function)
      RooAbsReal* nll = pdf->createNLL(*data, CloneData(kTRUE), Constrain(*constrainedParams));
      NLLatCondMLE = nll->getVal();
      delete nll;
   }

   // Use Wilks' theorem to translate -2 log lambda into a signifcance/p-value
   Double_t deltaNLL = std::max( NLLatCondMLE-NLLatMLE, 0.);

   TString name = TString("ProfileLRHypoTestResult_") + TString(GetName() ); 
   HypoTestResult* htr = 
      new HypoTestResult(name, SignificanceToPValue(sqrt( 2*deltaNLL)), 0 );


   // restore previous value of poi
   for (unsigned int i = 0; i < oldValues.size(); ++i) { 
      RooRealVar * mytarget = (RooRealVar*) constrainedParams->find(poiList[i].GetName());
      if (mytarget) { 
         mytarget->setVal(oldValues[i] ); 
         mytarget->setConstant(false); 
      }
   }

   delete constrainedParams;
   return htr;

}


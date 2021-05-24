//
// ********************************************************************
// * License and Disclaimer                                           *
// *                                                                  *
// * The  Geant4 software  is  copyright of the Copyright Holders  of *
// * the Geant4 Collaboration.  It is provided  under  the terms  and *
// * conditions of the Geant4 Software License,  included in the file *
// * LICENSE and available at  http://cern.ch/geant4/license .  These *
// * include a list of copyright holders.                             *
// *                                                                  *
// * Neither the authors of this software system, nor their employing *
// * institutes,nor the agencies providing financial support for this *
// * work  make  any representation or  warranty, express or implied, *
// * regarding  this  software system or assume any liability for its *
// * use.  Please see the license in the file  LICENSE  and URL above *
// * for the full disclaimer and the limitation of liability.         *
// *                                                                  *
// * This  code  implementation is the result of  the  scientific and *
// * technical work of the GEANT4 collaboration.                      *
// * By using,  copying,  modifying or  distributing the software (or *
// * any work based  on the software)  you  agree  to acknowledge its *
// * use  in  resulting  scientific  publications,  and indicate your *
// * acceptance of all terms of the Geant4 Software license.          *
// ********************************************************************
//
/// \file field/field02/src/F02ElectricFieldSetup.cc
/// \brief Implementation of the F02ElectricFieldSetup class
//
//
//
//   User Field class implementation.
//
//
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......




#include "F02ElectricFieldSetup.hh"
#include "F02FieldMessenger.hh"

#include "G4UniformElectricField.hh"
#include "G4UniformMagField.hh"
#include "G4MagneticField.hh"
#include "G4FieldManager.hh"
#include "G4TransportationManager.hh"
#include "G4EquationOfMotion.hh"
#include "G4EqMagElectricField.hh"
#include "G4Mag_UsualEqRhs.hh"
#include "G4MagIntegratorStepper.hh"
#include "G4MagIntegratorDriver.hh"
#include "G4ChordFinder.hh"

#include "G4ExplicitEuler.hh"
#include "G4ImplicitEuler.hh"
#include "G4SimpleRunge.hh"
#include "G4SimpleHeum.hh"
#include "G4ClassicalRK4.hh"
#include "G4HelixExplicitEuler.hh"
#include "G4HelixImplicitEuler.hh"
#include "G4HelixSimpleRunge.hh"
#include "G4CashKarpRKF45.hh"
#include "G4RKG3_Stepper.hh"

#include "G4PhysicalConstants.hh"
#include "G4SystemOfUnits.hh"

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

//  Constructors:

F02ElectricFieldSetup::F02ElectricFieldSetup()
 : fMinStep(0.001*mm),  // minimal step of 10 microns
   fFieldManager(0),
   fLocalFieldManager(0),
   fChordFinder(0),
   fLocalChordFinder(0),
   fEquation(0),
   fLocalEquation(0),
   fEMfield(0),
   fLocalEMfield(0),
   fElFieldValue(),
   fStepper(0),
   fLocalStepper(0),
   fIntgrDriver(0),
   fStepperType(4),    // ClassicalRK4 -- the default stepper
   fFieldMessenger(nullptr)   
{
  fEMfield = new G4UniformElectricField(
                   G4ThreeVector(0.0, 0.0, 0.0));
  fLocalEMfield = new G4UniformElectricField(
                   G4ThreeVector(0.0, 0.0, -3600*volt/m));
                   
  fEquation = new G4EqMagElectricField(fEMfield);
  fLocalEquation = new G4EqMagElectricField(fLocalEMfield);

  fFieldManager = GetGlobalFieldManager();
  fLocalFieldManager = new G4FieldManager();

  UpdateIntegrator();
  fFieldMessenger = new F02FieldMessenger(this);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

F02ElectricFieldSetup::F02ElectricFieldSetup(G4ThreeVector fieldVector)
  : fMinStep(0.001*mm),  // minimal step of 10 microns
    fFieldManager(0),
    fChordFinder(0),
    fEquation(0),
    fEMfield(0),
    fElFieldValue(),
    fStepper(0),
    fIntgrDriver(0),
    fStepperType(4),    // ClassicalRK4 -- the default stepper
    fFieldMessenger(nullptr)
{
  fEMfield = new G4UniformElectricField(fieldVector);
  fEquation = new G4EqMagElectricField(fEMfield);

  fFieldManager = GetGlobalFieldManager();
  UpdateIntegrator();
  
  fFieldMessenger = new F02FieldMessenger(this);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

F02ElectricFieldSetup::~F02ElectricFieldSetup()
{
  // G4cout << " F02ElectricFieldSetup - dtor called. " << G4endl;

  delete fFieldMessenger; fFieldMessenger= nullptr;
   // Delete the messenger first, to avoid messages to deleted classes!
  
  delete fChordFinder;  fChordFinder= nullptr;
  delete fStepper;      fStepper = nullptr;
  delete fEquation;     fEquation = nullptr;
  delete fEMfield;      fEMfield = nullptr;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

void F02ElectricFieldSetup::UpdateIntegrator()
{
  // Register this field to 'global' Field Manager and
  // Create Stepper and Chord Finder with predefined type, minstep (resp.)

  // It must be possible to call 'again' after an alternative stepper
  //   has been chosen, or other changes have been made
  assert(fEquation!=nullptr);

  G4cout<< " F02ElectricFieldSetup: The minimal step is equal to "
        << fMinStep/mm << " mm" << G4endl;

  
     delete fChordFinder;
     fChordFinder= nullptr;
     delete fLocalChordFinder;
     fLocalChordFinder= nullptr;
     // The chord-finder's destructor deletes the driver
     fIntgrDriver= nullptr;
 
  
  // Currently driver does not 'own' stepper      ( 17.05.2017 J.A. )
  //   -- so this stepper is still a valid object after this

     delete fStepper;
     fStepper = nullptr;
     delete fLocalStepper;
     fLocalStepper = nullptr;


  
  // Create the new objects, in turn for all relevant classes
  //  -- Careful to call this after all old objects are destroyed, and
  //      pointers nullified.
  CreateStepper();  // Note that this method deleted the existing Stepper!
  // G4cout << "F02ElectricFieldSetup::UpdateIntegrator> "
  //        << "New value of stepper ptr= " << fStepper << G4endl;
  assert(fStepper != nullptr);

  if( fStepper ) {
     fIntgrDriver = new G4MagInt_Driver(fMinStep,
                                        fStepper,
                                        fStepper->GetNumberOfVariables());
     if( fIntgrDriver ){ 
        fChordFinder = new G4ChordFinder(fIntgrDriver);
     }
  }
  
   if( fLocalStepper ) {
     fIntgrDriver = new G4MagInt_Driver(fMinStep,
                                        fLocalStepper,
                                        fLocalStepper->GetNumberOfVariables());
     if( fIntgrDriver ){ 
        fLocalChordFinder = new G4ChordFinder(fIntgrDriver);
     }
  }


  fFieldManager->SetChordFinder(fChordFinder);
  fLocalFieldManager->SetChordFinder(fLocalChordFinder);
  fFieldManager->SetDetectorField(fEMfield);
  fLocalFieldManager->SetDetectorField(fLocalEMfield);
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

void F02ElectricFieldSetup::CreateStepper()
{
  // Deletes the existing stepper
  //   and creates a new stepper object of the chosen stepper type

  const G4int nvar = 8;

  auto oldStepper= fStepper;

  switch ( fStepperType )
  {
    case 0:
      fStepper = new G4ExplicitEuler( fEquation, nvar );
      fLocalStepper = new G4ExplicitEuler( fLocalEquation, nvar );
      G4cout<<"G4ExplicitEuler is calledS"<<G4endl;
      break;
    case 1:
      fStepper = new G4ImplicitEuler( fEquation, nvar );
      fLocalStepper = new G4ImplicitEuler( fLocalEquation, nvar );
      G4cout<<"G4ImplicitEuler is called"<<G4endl;
      break;
    case 2:
      fStepper = new G4SimpleRunge( fEquation, nvar );
      fLocalStepper = new G4SimpleRunge( fLocalEquation, nvar );
      G4cout<<"G4SimpleRunge is called"<<G4endl;
      break;
    case 3:
      fStepper = new G4SimpleHeum( fEquation, nvar );
      fLocalStepper = new G4SimpleHeum( fLocalEquation, nvar );
      G4cout<<"G4SimpleHeum is called"<<G4endl;
      break;
    case 4:
      fStepper = new G4ClassicalRK4( fEquation, nvar );
      fLocalStepper = new G4ClassicalRK4( fLocalEquation, nvar );
      G4cout<<"G4ClassicalRK4 is called"<<G4endl;
      break;
    case 5:
      fStepper = new G4CashKarpRKF45( fEquation, nvar );
      fLocalStepper = new G4CashKarpRKF45( fLocalEquation, nvar );
      G4cout<<"G4CashKarpRKF45 is called"<<G4endl;
      break;
    case 6:
      fStepper = 0; 
       fLocalStepper = 0;// new G4RKG3_Stepper( fEquation, nvar );
      G4cout<<"G4RKG3_Stepper is not currently working for Electric Field"
            <<G4endl;
      break;
    case 7:
      fStepper = 0;
      fLocalStepper = 0; // new G4HelixExplicitEuler( fEquation );
      G4cout<<"G4HelixExplicitEuler is not valid for Electric Field"<<G4endl;
      break;
    case 8:
      fStepper = 0;
      fLocalStepper = 0; // new G4HelixImplicitEuler( fEquation );
      G4cout<<"G4HelixImplicitEuler is not valid for Electric Field"<<G4endl;
      break;
    case 9:
      fStepper = 0;
      fLocalStepper = 0; // new G4HelixSimpleRunge( fEquation );
      G4cout<<"G4HelixSimpleRunge is not valid for Electric Field"<<G4endl;
      break;
    default:  
      fStepper = new G4ClassicalRK4( fEquation, nvar );
      fLocalStepper = new G4ClassicalRK4( fLocalEquation, nvar );
      G4cout<<"G4ClassicalRK4 (default) is called"<<G4endl;
      break;
  }

  delete oldStepper;
  // Now must make sure it is 'stripped' from the dependent object(s)
  //  ... but the next line does this anyway - by informing
  //      the driver (if it exists) about the new stepper.

  // Always inform the (existing) driver about the new stepper
  if( fIntgrDriver )
      fIntgrDriver->RenewStepperAndAdjust( fStepper );
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

void F02ElectricFieldSetup::SetFieldZValue(G4double fieldValue)
{
  // Set the value of the Global Field to fieldValue along Z

  G4ThreeVector fieldVector( 0.0, 0.0, fieldValue );

  SetFieldValue( fieldVector );
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

void F02ElectricFieldSetup::SetFieldValue(G4ThreeVector fieldVector)
{
  if (fEMfield) delete fEMfield;

  // Set the value of the Global Field value to fieldVector

  // Find the Field Manager for the global field
  G4FieldManager* fieldMgr= GetGlobalFieldManager();

  if (fieldVector != G4ThreeVector(0.,0.,0.))
  {
    fEMfield = new G4UniformElectricField(fieldVector);
  }
  else
  {
    // If the new field's value is Zero, then it is best to
    //  insure that it is not used for propagation.
    fEMfield = 0;
  }
  fieldMgr->SetDetectorField(fEMfield);
  fEquation->SetFieldObj(fEMfield);  // must now point to the new field
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
void F02ElectricFieldSetup::SetLocalFieldValue(G4ThreeVector fieldVector)
{
  if(fLocalEMfield) delete fLocalEMfield;

  if(fieldVector != G4ThreeVector(0.,0.,0.))
  {
    fLocalEMfield = new  G4UniformElectricField(fieldVector);
  }
  else
  {
    // If the new field's value is Zero, then
    // setting the pointer to zero ensures
    // that it is not used for propagation.
    fEMfield = 0;
  }

  // Either
  //   - UpdateField() to reset all (ChordFinder, Equation);
  // UpdateField();
  //     or simply update the field manager & equation of motion
  //     with pointer to new field
  GetLocalFieldManager()->SetDetectorField(fLocalEMfield);
  fEquation->SetFieldObj( fLocalEMfield );
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

G4FieldManager* F02ElectricFieldSetup::GetGlobalFieldManager()
{
//  Utility method

  return G4TransportationManager::GetTransportationManager()
           ->GetFieldManager();
}


//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......


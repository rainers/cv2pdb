// Convert DMD CodeView debug information to PDB files
// Copyright (c) 2009 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details

// CLR interface to cv2pdb created by Alexander Bothe

#include "../PEImage.h"
#include "../cv2pdb.h"
#include "vcclr.h"

using namespace System;
using namespace System::IO;
using namespace System::Text;

namespace CodeViewToPDB
{
	///<summary>Exports DMD CodeView debug information from an executable file to a separate .pdb file</summary>
	public ref class CodeViewToPDBConverter
	{
	public:
		delegate void MsgHandler(String^ Message);
		///<summary>If an error occurs it will be reported via this event</summary>
		static event MsgHandler^ Message;
		
		///<summary>Exports DMD CodeView debug information from an executable file to a separate .pdb file</summary>
		static bool DoConvert(String^ InputExe,String^ OutputExe,String^ OutputPDBFile,bool IsD2)
		{
			if(!File::Exists(InputExe))
			{
				Message("Input file doesn't exist!");
				return false;
			}
			
			if(String::IsNullOrEmpty(OutputPDBFile) || String::IsNullOrEmpty(OutputExe))
			{
				Message("Empty arguments not allowed!");
				return false;
			}
		
			char* input=(char*)System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(InputExe).ToPointer();
			char* outname=(char*)System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(OutputExe).ToPointer();
			char* pdbname=(char*)System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(OutputPDBFile).ToPointer();

			PEImage img(input);

			if (img.countCVEntries() == 0)
			{
				Message("No codeview debug entries found");
				return false;
			}

			CV2PDB cv2pdb(img);
			cv2pdb.initLibraries();
			cv2pdb.Dversion = IsD2?2:1;

			File::Delete(OutputPDBFile);

			if(cv2pdb.openPDB(pdbname) 
			&& cv2pdb.initSegMap() 
			&& cv2pdb.initGlobalTypes()
			&& cv2pdb.createModules()
			&& cv2pdb.addTypes()
			&& cv2pdb.addSymbols()
			&& cv2pdb.addSrcLines()
			&& cv2pdb.addPublics()
			&& cv2pdb.writeImage(outname))
			{	}
			else
			{
				Message(gcnew String(cv2pdb.getLastError()));
				return false;
			}
			
			return true;
		}
	
		static bool DoConvert(String^ Exe,String^ OutputPDBFile)
		{
			return DoConvert(Exe,Exe,OutputPDBFile,true);
		}
		
		static bool DoConvert(String^ Exe)
		{
			return DoConvert(Exe,Exe,Path::ChangeExtension(Exe,".pdb"),true);
		}
	};
}

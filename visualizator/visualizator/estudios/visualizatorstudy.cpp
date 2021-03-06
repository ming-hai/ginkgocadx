/*
 * This file is part of Ginkgo CADx
 *
 * Copyright (c) 2015-2016 Gert Wollny
 * Copyright (c) 2008-2014 MetaEmotion S.L. All rights reserved.
 *
 * Ginkgo CADx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser Public License
 * along with Ginkgo CADx; if not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <wx/app.h>
#include <wx/xml/xml.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/mstream.h>

#include <api/globals.h>
//#include <api/filename.h>
#include <api/toolsystem/itoolsregistry.h>
#include <api/ientorno.h>

#include <api/dicom/idicommanager.h>
#include <api/dicom/idicomizador.h>
#include <api/controllers/icontroladorcarga.h>
#include <api/imodelointegracion.h>
#include <main/controllers/historycontroller.h>

#include <main/entorno.h>

#include "visualizatorstudy.h"

#include <vtkPointData.h>
#include <vtkImageData.h>


GNKVisualizator::VisualizatorStudy::VisualizatorStudy()
{
        GTRACE(">> VisualizatorStudy::VisualizatorStudy()" << this);
        GTRACE("<< VisualizatorStudy::VisualizatorStudy()" << this);
}

GNKVisualizator::VisualizatorStudy::~VisualizatorStudy()
{
        GTRACE(">> VisualizatorStudy::~VisualizatorStudy()" << this);
        ListaOverlays.clear();
        GTRACE("<< VisualizatorStudy::~VisualizatorStudy()" << this);
}


void GNKVisualizator::VisualizatorStudy::InitializeContext(long seriesPk)
{
        GNC::GCS::IHistoryController::LightFileModelList fileModels;
        GNC::GCS::HistoryController::Instance()->GetSeriesSortedFileModels(seriesPk, fileModels);

        std::list<std::string> rutas;
        for (GNC::GCS::IHistoryController::LightFileModelList::iterator itFile = fileModels.begin(); itFile != fileModels.end(); ++itFile) {
                rutas.push_back((*itFile).real_path);
        }

        GNC::GCS::IStudyContext::DoInitiallizeContext(rutas);
        ListaOverlays.clear();
        for (std::list<std::string>::const_iterator it = rutas.begin(); it != rutas.end(); ++it) {
                ListaOverlays.push_back(NULL);
        }
        Loader->SetInput(rutas.front());
}

bool GNKVisualizator::VisualizatorStudy::TieneOverlaysImagenActiva()
{
        return TieneOverlaysImagen(ActiveFileIndex);
}

bool GNKVisualizator::VisualizatorStudy::TieneOverlaysImagen(const int indice)
{
        //se pillan los overlays de la imagen original
        if(ListaOverlays[indice].IsValid()) {
                return ListaOverlays[indice]->size()>0;
        } else {
                GNC::GCS::Ptr<GIL::DICOM::DicomDataset> jerarq = GetTagsImage(indice);
                if (jerarq.IsValid()) {
                        for(GIL::DICOM::ListaTags::iterator it = jerarq->tags.begin(); it != jerarq->tags.end(); ++it) {
                                if((*it).first.substr(0,2) == "60") {
                                        return true;
                                }
                        }
                        return false;
                } else {
                        return false;
                }
        }
}

GNC::GCS::Ptr<GNKVisualizator::TListaOverlays> GNKVisualizator::VisualizatorStudy::GetOverlaysImagenActiva()
{
        return GetOverlaysImagen(ActiveFileIndex);
}

GNC::GCS::Ptr<GNKVisualizator::TListaOverlays> GNKVisualizator::VisualizatorStudy::GetOverlaysImagen(const int indice)
{
        if(!ListaOverlays[indice].IsValid()) {
                ListaOverlays[indice] = new TListaOverlays();

                GIL::DICOM::IDICOMManager* pDICOMManager= GNC::GCS::IEntorno::Instance()->GetPACSController()->CrearInstanciaDeDICOMManager();
                pDICOMManager->CargarFichero(Files[indice]->PathOfFile);
                int filasImagen,columnasImagen;
                //los overlays van en los grupos pares de 6000 a 601E, ver parte 5 del standard "repeating groups"
                if(pDICOMManager->GetTag(0x0028,0x0010,filasImagen) && pDICOMManager->GetTag(0x0028,0x0011,columnasImagen)) {
                        int idOver = 0;
                        for(int grupo = 0x6000; grupo <0x601E; grupo+=2) {
                                int filas,columnas;
                                std::string nombreCapa;
                                std::string origen, tipo;
                                GIL::DICOM::TagPrivadoUndefined overlayData;
                                int bitPosition,bitsAllocated;
                                if(pDICOMManager->GetTag(grupo,0x0010,filas) && pDICOMManager->GetTag(grupo,0x0011,columnas)
                                    && pDICOMManager->GetTag(grupo,0x0102,bitPosition) && pDICOMManager->GetTag(grupo,0x0100,bitsAllocated)) {
                                        //TODO: si no es del tamanio de la imagen de momento no lo tratamos, tendremos que crear una imagen del tamanio adecuado y pegar el roi en su sitio
                                        if(filas!= filasImagen || columnas!=columnasImagen) {
                                                continue;
                                        }
                                        pDICOMManager->GetTag(grupo,0x0040,tipo);
                                        //TODO TRATAR DISTINTOS A LOS ROI por ejemplo mostrar la media y todo eso...
                                        if(!pDICOMManager->GetTag(grupo,0x0022,nombreCapa)) {
                                                std::ostringstream ostrNombre;
                                                ostrNombre <<"Capa " << idOver;
                                                nombreCapa = ostrNombre.str();
                                        }
                                        TOverlay over(nombreCapa,grupo);
                                        idOver++;

                                        if(bitPosition==0 && bitsAllocated==1 && pDICOMManager->GetTag(grupo,0x3000,overlayData)) {
                                                //este es el caso en el que la capa overlay esta especificada aparte, no usando bits d la imagen
                                                //se copia la imagen
                                                vtkImageData* pimg = vtkImageData::New();
                                                over.img = pimg;
                                                pimg->Delete();
                                                over.img->SetDimensions(columnas,filas,1);
                                                {
                                                        float x,y;
                                                        x=1.0f;
                                                        y=1.0f;
                                                        if(pDICOMManager->GetTag(grupo,0x0050,origen)) {
                                                                std::istringstream issl(origen);
                                                                issl >> x;
                                                                if(!issl.eof()) {
                                                                        char c;
                                                                        issl>>c;//la barra
                                                                        issl>>y;
                                                                }
                                                        }
                                                        over.img->SetOrigin(x,y,1.0f);
                                                }
                                                over.img->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

                                                unsigned char* pData = (unsigned char*)over.img->GetScalarPointer();
                                                const unsigned char * ptr = (const unsigned char *)overlayData.GetValor();

                                                int size = overlayData.GetSize();
                                                int sizeOfImage = filas*columnas;
                                                //double tuple[1] = {0.0f};
                                                char tmp,c;
                                                int off = 0;
                                                for(int i=0; i<size; i++) {
                                                        c = (const unsigned char) (ptr[i]);
                                                        tmp = 1;
                                                        for(int j=0; j<8 && off<sizeOfImage; j++) {
                                                                if((tmp & c)==0) {
                                                                        pData[off++] = 0;
                                                                } else {
                                                                        pData[off++] = 1;
                                                                }
                                                                tmp<<=1;
                                                        }
                                                }
                                                ListaOverlays[indice]->push_back(over);
                                        } else {
                                                int bitsAllocatedImagen;
                                                //en este caso el overlay esta en el pixeldata, esta retired pero asi funcionan tb los viejos
                                                if(pDICOMManager->GetTag(0x0028,0x0100,bitsAllocatedImagen) && pDICOMManager->GetTag(0x7fe0,0x0010,overlayData) && overlayData.GetSize() > 0) {
                                                        vtkImageData* tmp = vtkImageData::New();
                                                        over.img = tmp;
                                                        tmp->Delete();
                                                        over.img->SetDimensions(columnas,filas,1);
                                                        {
                                                                float x,y;
                                                                x=1.0f;
                                                                y=1.0f;
                                                                if(pDICOMManager->GetTag(grupo,0x0050,origen)) {
                                                                        std::istringstream issl(origen);
                                                                        issl >> x;
                                                                        if(!issl.eof()) {
                                                                                char c;
                                                                                issl>>c;//la barra
                                                                                issl>>y;
                                                                        }
                                                                }
                                                                over.img->SetOrigin(x,y,1.0f);
                                                        }
                                                        over.img->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

                                                        unsigned char* pData = (unsigned char*)over.img->GetScalarPointer();
                                                        const unsigned char * ptr = (const unsigned char *)overlayData.GetValor();

                                                        int size = overlayData.GetSize();
                                                        //int sizeOfImage = filas*columnas;
                                                        //double tuple[1] = {0.0f};
                                                        if(bitsAllocatedImagen == 8 && bitPosition<8) {
                                                                //se itera caracter a caracter...
                                                                char mascara,c;
                                                                int off = 0;
                                                                mascara = 1;
                                                                mascara <<= bitPosition;
                                                                int desplazamiento;
                                                                for (int y = 0; y < filas; y++) {
                                                                        desplazamiento = y*columnas;
                                                                        for (int x = 0; x < columnas && off < size; x++) {
                                                                                c = (unsigned char) *(ptr + desplazamiento + x);
                                                                                if((mascara & c)==0) {
                                                                                        pData[off++] = 0;
                                                                                } else {
                                                                                        pData[off++] = 1;
                                                                                }
                                                                        }
                                                                }
                                                                ListaOverlays[indice]->push_back(over);
                                                        } else if(bitsAllocatedImagen == 16 && bitPosition<16) {
                                                                //se itera con 16 bits...
                                                                unsigned char mascara;
                                                                unsigned char* c;
                                                                int off=0;
                                                                unsigned char bitAConsultar = 0;
                                                                if(bitPosition<8) {
                                                                        mascara = 1;
                                                                        mascara <<= bitPosition;
                                                                } else {
                                                                        mascara = 1;
                                                                        mascara <<= (bitPosition-8);
                                                                        bitAConsultar = 1;
                                                                }
                                                                int desplazamiento;
                                                                for (int y = 0; y < filas; y++) {
                                                                        desplazamiento = y*columnas;
                                                                        for (int x = 0; x < columnas && off < size; ++x) {
                                                                                c = (unsigned char*) (ptr + 2*(desplazamiento + x)+bitAConsultar);
                                                                                if((mascara & (*c))==0) {
                                                                                        pData[off++] = 0;
                                                                                } else {
                                                                                        pData[off++] = 1;
                                                                                }
                                                                        }
                                                                }
                                                                ListaOverlays[indice]->push_back(over);
                                                        }//else bits allocated
                                                }//if get pixel data y comprobaciones
                                        }//else overlay por separado
                                }//if columnas, bits...
                        }//for overlays
                }//si esta especificado filas/columnas de la imagen
                GNC::GCS::IEntorno::Instance()->GetPACSController()->LiberarInstanciaDeDICOMManager(pDICOMManager);
        }
        return ListaOverlays[indice];
}


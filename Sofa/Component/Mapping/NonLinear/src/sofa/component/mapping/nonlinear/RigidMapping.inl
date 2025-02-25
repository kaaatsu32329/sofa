/******************************************************************************
*                 SOFA, Simulation Open-Framework Architecture                *
*                    (c) 2006 INRIA, USTL, UJF, CNRS, MGH                     *
*                                                                             *
* This program is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This program is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this program. If not, see <http://www.gnu.org/licenses/>.        *
*******************************************************************************
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#pragma once
#include <sofa/component/mapping/nonlinear/RigidMapping.h>
#include <sofa/core/visual/VisualParams.h>

#include <sofa/core/behavior/MechanicalState.h>
#include <sofa/core/State.h>

#include <sofa/defaulttype/VecTypes.h>
#include <sofa/defaulttype/RigidTypes.h>

#include <sofa/helper/io/XspLoader.h>
#include <sofa/helper/io/SphereLoader.h>
#include <sofa/helper/io/Mesh.h>
#include <sofa/helper/decompose.h>
#include <sofa/core/MechanicalParams.h>

#include <Eigen/Dense>

#include <cstring>
#include <istream>

namespace sofa::component::mapping::nonlinear
{

template <class TIn, class TOut>
class RigidMapping<TIn, TOut>::Loader :
        public helper::io::XspLoaderDataHook,
        public helper::io::SphereLoaderDataHook
{
public:

    RigidMapping<TIn, TOut>* dest;
    helper::WriteAccessor<Data<VecCoord> > points;

    Loader(RigidMapping<TIn, TOut>* dest) :
        dest(dest),
        points(dest->points)
    {
    }
    void addMass(SReal px, SReal py, SReal pz, SReal, SReal, SReal,
                         SReal, SReal, bool, bool) override
    {
        Coord c;
        Out::set(c, px, py, pz);
        points.push_back(c); //Coord((Real)px,(Real)py,(Real)pz));
    }
    void addSphere(SReal px, SReal py, SReal pz, SReal) override
    {
        Coord c;
        Out::set(c, px, py, pz);
        points.push_back(c); //Coord((Real)px,(Real)py,(Real)pz));
    }
};

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::load(const char *filename)
{
    points.beginEdit()->resize(0);
    points.endEdit();

    if (strlen(filename) > 4
            && !strcmp(filename + strlen(filename) - 4, ".xs3"))
    {
        Loader loader(this);
        helper::io::XspLoader::Load(filename, loader);
    }
    else if (strlen(filename) > 4
             && !strcmp(filename + strlen(filename) - 4, ".sph"))
    {
        Loader loader(this);
        helper::io::SphereLoader::Load(filename, loader);
    }
    else if (strlen(filename) > 0)
    {
        // Default to mesh loader
        helper::io::Mesh* mesh = helper::io::Mesh::Create(filename);
        if (mesh != nullptr)
        {
            helper::WriteAccessor<Data<VecCoord> > points = this->points;

            points.resize(mesh->getVertices().size());
            for (unsigned int i = 0; i < mesh->getVertices().size(); i++)
            {
                Out::set(points[i],
                         mesh->getVertices()[i][0],
                         mesh->getVertices()[i][1],
                         mesh->getVertices()[i][2]);
            }
            delete mesh;
        }
    }
}


template <class TIn, class TOut>
RigidMapping<TIn, TOut>::RigidMapping()
    : Inherit()
    , points(initData(&points, "initialPoints", "Local Coordinates of the points"))
    , index(initData(&index, (unsigned)0, "index", "input DOF index"))
    , fileRigidMapping(initData(&fileRigidMapping, "filename", "Xsp file where rigid mapping information can be loaded from."))
    , useX0(initData(&useX0, false, "useX0", "Use x0 instead of local copy of initial positions (to support topo changes)"))
    , indexFromEnd(initData(&indexFromEnd, false, "indexFromEnd", "input DOF index starts from the end of input DOFs vector"))
    , rigidIndexPerPoint(initData(&rigidIndexPerPoint, "rigidIndexPerPoint", "For each mapped point, the index of the Rigid it is mapped from"))
    , globalToLocalCoords(initData(&globalToLocalCoords, "globalToLocalCoords", "are the output DOFs initially expressed in global coordinates"))
    , matrixJ()
    , updateJ(false)
{
    this->addAlias(&fileRigidMapping, "fileRigidMapping");
    sofa::helper::getWriteAccessor(this->d_geometricStiffness)->setSelectedItem(0);
}

template <class TIn, class TOut>
sofa::Index RigidMapping<TIn, TOut>::getRigidIndex(sofa::Index pointIndex ) const
{
    // do we really need this crap?
    if( points.getValue().size() == rigidIndexPerPoint.getValue().size() ) return rigidIndexPerPoint.getValue()[pointIndex];
    else
    {
        if( !indexFromEnd.getValue() ) return index.getValue();
        else return this->fromModel->getSize()-1-index.getValue();
    }
}

template <class TIn, class TOut>
sofa::Size RigidMapping<TIn, TOut>::addPoint(const Coord& c)
{
    helper::WriteAccessor<Data<VecCoord> > points = this->points;
    sofa::Size i = sofa::Size(points.size());
    points.push_back(c);
    return i;
}

template <class TIn, class TOut>
sofa::Size RigidMapping<TIn, TOut>::addPoint(const Coord& c, sofa::Index indexFrom)
{
    VecCoord& points = *this->points.beginEdit();
    sofa::Size i = sofa::Size(points.size());
    points.push_back(c);
    this->points.endEdit();

    type::vector<unsigned int>& rigidIndexPerPoint = *this->rigidIndexPerPoint.beginEdit();

    if( i && rigidIndexPerPoint.size()!=i )
    {
        rigidIndexPerPoint.resize( i+1 );
        std::fill( rigidIndexPerPoint.begin(), rigidIndexPerPoint.end()-1, getRigidIndex(0) );
    }
    else rigidIndexPerPoint.push_back(indexFrom);

    this->rigidIndexPerPoint.endEdit();
    return i;
}


template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::reinit()
{
    if (this->points.getValue().empty() && this->toModel != nullptr && !useX0.getValue())
    {
        const VecCoord& xTo =this->toModel->read(core::ConstVecCoordId::position())->getValue();
        helper::WriteOnlyAccessor<Data<VecCoord> > points = this->points;
        points.resize(xTo.size());
        unsigned int i = 0;
        if (globalToLocalCoords.getValue())
        {
            unsigned int i = 0;
            const InVecCoord& xFrom =this->fromModel->read(core::ConstVecCoordId::position())->getValue();

            for (i = 0; i < points.size(); i++)
            {
                unsigned int rigidIndex = getRigidIndex(i);
                points[i] = xFrom[rigidIndex].inverseRotate(xTo[i] - xFrom[rigidIndex].getCenter());
            }
        }
        else
        {
            for (i = 0; i < xTo.size(); i++)
            {
                points[i] = xTo[i];
            }
        }
    }
}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::init()
{
    if (!fileRigidMapping.getValue().empty())
        this->load(fileRigidMapping.getFullPath().c_str());

    eigenJacobians.resize( 1 );
    eigenJacobians[0] = &eigenJacobian;

    this->reinit();

    this->Inherit::init();
}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::clear(sofa::Size reserve)
{
    helper::WriteOnlyAccessor<Data<VecCoord> > points = this->points;
    points.clear();
    if (reserve)
        points.reserve(reserve);
    this->rigidIndexPerPoint.beginWriteOnly()->clear();
    this->rigidIndexPerPoint.endEdit();
}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::setRepartition(sofa::Size value)
{
    msg_deprecated()<<"setRepartition function. Fill rigidIndexPerPoint instead.";

    type::vector<unsigned int>& rigidIndexPerPoint = *this->rigidIndexPerPoint.beginWriteOnly();

    size_t size = this->toModel->getSize();

    rigidIndexPerPoint.resize( size );

    unsigned int idx = 0;
    for( size_t i=0 ; i<size ; )
    {
         for( size_t j=0; j<value && i<size ; ++j, ++i )
         {
            rigidIndexPerPoint[i] = idx;
         }
         ++idx;
    }

    this->rigidIndexPerPoint.endEdit();
}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::setRepartition(sofa::type::vector<sofa::Size> values)
{
    msg_deprecated()<<"setRepartition function. Fill rigidIndexPerPoint instead.";

    type::vector<unsigned int>& rigidIndexPerPoint = *this->rigidIndexPerPoint.beginWriteOnly();

    size_t size = this->toModel->getSize();

    rigidIndexPerPoint.resize( size );

    size_t i = 0;
    for( unsigned int idx=0 ; idx<values.size() ; ++idx )
    {
         for( size_t j=0, jend=values[idx]; j<jend ; ++j, ++i )
         {
            rigidIndexPerPoint[i] = idx;
         }
    }

    this->rigidIndexPerPoint.endEdit();
}

template <class TIn, class TOut>
const typename RigidMapping<TIn, TOut>::VecCoord & RigidMapping<TIn, TOut>::getPoints()
{
    if (useX0.getValue())
    {
        const Data<VecCoord>* v = this->toModel.get()->read(core::VecCoordId::restPosition());
        if (v)
        {
            return v->getValue();
        }
        else
        {
            msg_error()<< "RigidMapping: ERROR useX0 can only be used in MechanicalMappings.";
        }
    }
    return points.getValue();
}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::apply(const core::MechanicalParams * /*mparams*/, Data<VecCoord>& dOut, const Data<InVecCoord>& dIn)
{
    helper::WriteOnlyAccessor< Data<VecCoord> > out = dOut;
    helper::ReadAccessor< Data<InVecCoord> > in = dIn;
    const VecCoord& pts = this->getPoints();

    updateJ = true;
    eigenJacobian.resizeBlocks(out.size(),in.size());

    rotatedPoints.resize(pts.size());
    out.resize(pts.size());

    for (sofa::Index i = 0; i < sofa::Size(pts.size()); i++)
    {
        sofa::Index rigidIndex = getRigidIndex(i);

        rotatedPoints[i] = in[rigidIndex].rotate( pts[i] );
        out[i] = in[rigidIndex].translate( rotatedPoints[i] );
    }
}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::applyJ(const core::MechanicalParams * /*mparams*/, Data<VecDeriv>& dOut, const Data<InVecDeriv>& dIn)
{
    helper::WriteOnlyAccessor< Data<VecDeriv> > out = dOut;
    helper::ReadAccessor< Data<InVecDeriv> > in = dIn;

    const VecCoord& pts = this->getPoints();
    out.resize(pts.size());

    for(sofa::Index i=0 ; i<out.size() ; ++i)
    {
        sofa::Index rigidIndex = getRigidIndex(i);
        out[i] = velocityAtRotatedPoint( in[rigidIndex], rotatedPoints[i] );
    }
}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::applyJT(const core::MechanicalParams * /*mparams*/, Data<InVecDeriv>& dOut, const Data<VecDeriv>& dIn)
{
    helper::WriteAccessor< Data<InVecDeriv> > out = dOut;
    helper::ReadAccessor< Data<VecDeriv> > in = dIn;

    for(sofa::Index i=0 ; i<in.size() ; ++i)
    {
        sofa::Index rigidIndex = getRigidIndex(i);

        getVCenter(out[rigidIndex]) += in[i];
        getVOrientation(out[rigidIndex]) += (typename InDeriv::Rot)cross(rotatedPoints[i], in[i]);
    }

}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::applyDJT(const core::MechanicalParams* mparams, core::MultiVecDerivId parentForceChangeId, core::ConstMultiVecDerivId childForceId)
{
    if( !d_geometricStiffness.getValue().getSelectedId() ) return;

    if( geometricStiffnessMatrix.compressedMatrix.nonZeros() ) // assembled version
    {
        auto InF = sofa::helper::getWriteOnlyAccessor(*parentForceChangeId[this->fromModel.get()].write());
        auto inDx = sofa::helper::getReadAccessor(*mparams->readDx(this->fromModel.get()));
        geometricStiffnessMatrix.addMult( InF.wref(), inDx.ref(), (InReal)mparams->kFactor() );
    }
    else
    {
        // if symmetrized version, force local assembly
        if( d_geometricStiffness.getValue().getSelectedId() == 2 )
        {
            updateK( mparams, childForceId );
            auto InF = sofa::helper::getWriteOnlyAccessor(*parentForceChangeId[this->fromModel.get()].write());
            auto inDx = sofa::helper::getReadAccessor(*mparams->readDx(this->fromModel.get()));

            geometricStiffnessMatrix.addMult( InF.wref(), inDx.ref(), (InReal)mparams->kFactor() );
            geometricStiffnessMatrix.resize(0,0); // forgot about this matrix
        }
        else
        {
            // This method corresponds to a non-symmetric matrix, due to the non-commutativity of the group of rotations.
            assert( !mparams->symmetricMatrix() );

            helper::ReadAccessor<Data<VecDeriv> > childForces (*mparams->readF(this->toModel.get()));
            helper::WriteAccessor<Data<InVecDeriv> > parentForces (*parentForceChangeId[this->fromModel.get()].write());
            helper::ReadAccessor<Data<InVecDeriv> > parentDisplacements (*mparams->readDx(this->fromModel.get()));
            InReal kfactor = (InReal)mparams->kFactor();

            for(sofa::Index i=0 ; i< childForces.size() ; ++i)
            {
                sofa::Index rigidIndex = getRigidIndex(i);

                typename TIn::AngularVector& parentTorque = getVOrientation(parentForces[rigidIndex]);
                const typename TIn::AngularVector& parentRotation = getVOrientation(parentDisplacements[rigidIndex]);
                const typename TIn::AngularVector& torqueDecrement = TIn::crosscross( childForces[i], parentRotation, rotatedPoints[i]) * kfactor;
                parentTorque -=  torqueDecrement;
            }
        }
    }

}


// RigidMapping::applyJT( InMatrixDeriv& out, const OutMatrixDeriv& in ) //
// this function propagate the constraint through the rigid mapping :
// if one constraint along (vector n) with a value (v) is applied on the childModel (like collision model)
// then this constraint is transformed by (Jt.n) with value (v) for the rigid model
// There is a specificity of this propagateConstraint: we have to find the application point on the childModel
// in order to compute the right constaint on the rigidModel.
template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::applyJT(const core::ConstraintParams * /*cparams*/, Data<InMatrixDeriv>& dOut, const Data<OutMatrixDeriv>& dIn)
{
    InMatrixDeriv& out = *dOut.beginEdit();
    const OutMatrixDeriv& in = dIn.getValue();

    dmsg_info() << "J on mapped DOFs == " << in << msgendl
                << "J on input  DOFs == " << out ;

    const unsigned int numDofs = this->getFromModel()->getSize();

    // TODO the implementation on the new data structure could maybe be optimized
    typename Out::MatrixDeriv::RowConstIterator rowItEnd = in.end();

    for (typename Out::MatrixDeriv::RowConstIterator rowIt = in.begin(); rowIt != rowItEnd; ++rowIt)
    {
        for (unsigned int ito = 0; ito < numDofs; ito++)
        {
            DPos v;
            DRot omega = DRot();
            bool needToInsert = false;

            for (typename Out::MatrixDeriv::ColConstIterator colIt = rowIt.begin(); colIt != rowIt.end(); ++colIt)
            {
                unsigned int rigidIndex = getRigidIndex( colIt.index() );
                if(rigidIndex != ito)
                    continue;

                needToInsert = true;
                const Deriv f = colIt.val();
                v += f;
                omega += (DRot) cross(rotatedPoints[colIt.index()], f);
            }

            if (needToInsert)
            {
                const InDeriv result(v, omega);

                typename InMatrixDeriv::RowIterator o = out.writeLine(rowIt.index());
                o.addCol(ito, result);
            }
        }
    }

    dmsg_info() << "new J on input  DOFs = " << out ;

    dOut.endEdit();
}


namespace impl {

template<class U, class Coord>
static void fill_block(Eigen::Matrix<U, 3, 6>& block, const Coord& v) {
    U x = v[0];
    U y = v[1];
    U z = v[2];

    // note: this is -hat(v)
    block.template rightCols<3>() <<

        0,   z,  -y,
        -z,  0,   x,
        y,  -x,   0;
}

template<class U, class Coord>
void fill_block(Eigen::Matrix<U, 2, 3>& block, const Coord& v) {
    U x = v[0];
    U y = v[1];

    // note: this is -hat(v)
    block.template rightCols<1>() <<
        -y,
        x;
}


}

template <class TIn, class TOut>
const type::vector<sofa::linearalgebra::BaseMatrix*>* RigidMapping<TIn, TOut>::getJs()
{
    const VecCoord& out =this->toModel->read(core::ConstVecCoordId::position())->getValue();
    const InVecCoord& in =this->fromModel->read(core::ConstVecCoordId::position())->getValue();

    typename SparseMatrixEigen::CompressedMatrix& J = eigenJacobian.compressedMatrix;

    if( updateJ || J.size() == 0 )
    {

        updateJ = false;

        J.resize(out.size() * NOut, in.size() * NIn);
        J.setZero();

        // matrix chunk
        typedef typename TOut::Real real;
        typedef Eigen::Matrix<real, NOut, NIn> block_type;
        block_type block;

        // translation part
        block.template leftCols<NOut>().setIdentity();



        for(sofa::Index outIdx=0 ; outIdx< rotatedPoints.size() ; ++outIdx)
        {
            sofa::Index inIdx = getRigidIndex(outIdx);

            const Coord& v = rotatedPoints[outIdx];

            impl::fill_block(block, v);

            // block is set, now insert it in sparse matrix
            for(unsigned i = 0; i < NOut; ++i){
                unsigned row = outIdx * NOut + i;

                J.startVec( row );

                // TODO optimize identity off-diagonal and
                // skew-symmetric diagonal
                for(unsigned j = 0; j < NIn; ++j) {
                    unsigned col = inIdx * NIn + j;

                    if( block(i, j) != 0 ) {

                        J.insertBack(row, col) = block(i, j);
                    }
                }
            }
        }

        J.finalize();
    }

    return &eigenJacobians;
}



template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::updateK( const core::MechanicalParams* mparams, core::ConstMultiVecDerivId childForceId )
{
    SOFA_UNUSED(mparams);
    const unsigned geomStiff = d_geometricStiffness.getValue().getSelectedId();

    if( !geomStiff ) { geometricStiffnessMatrix.resize(0,0); return; }

    typedef typename StiffnessSparseMatrixEigen::CompressedMatrix matrix_type;
    matrix_type& dJ = geometricStiffnessMatrix.compressedMatrix;

    size_t insize = TIn::deriv_total_size*this->fromModel->getSize();

    dJ.resize( insize, insize );
    dJ.setZero(); // necessary ?

    const VecDeriv& childForces = childForceId[this->toModel.get()].read()->getValue();

    // sorted in-out
    typedef std::map<unsigned, type::vector<unsigned> > in_out_type;
    in_out_type in_out;

    // wahoo it is heavy, can't we find lighter?
    for(sofa::Index i = 0, n = rotatedPoints.size(); i < n; ++i)
        in_out[ getRigidIndex(i) ].push_back(i);

    for( in_out_type::const_iterator it = in_out.begin(), end = in_out.end() ; it != end; ++it )
    {
        const unsigned rigidIdx = it->first;

        static const unsigned rotation_dimension = TIn::deriv_total_size - TIn::spatial_dimensions;

        type::Mat<rotation_dimension,rotation_dimension,Real> block;


        for( unsigned int w=0 ; w<it->second.size() ; ++w )
        {
            const unsigned pointIdx = it->second[w];
            block += type::crossProductMatrix<Real>( childForces[pointIdx] ) * type::crossProductMatrix<Real>( rotatedPoints[pointIdx] );
        }

        if( geomStiff == 2 )
        {
            block.symmetrize(); // symmetrization
            helper::Decompose<Real>::NSDProjection( block ); // negative, semi-definite projection
        }

        for(unsigned j = 0; j < rotation_dimension; ++j) {

            const unsigned row = TIn::deriv_total_size * rigidIdx + TIn::spatial_dimensions + j;

            dJ.startVec( row );

            for(unsigned k = 0; k < rotation_dimension; ++k) {
                const unsigned col = TIn::deriv_total_size * rigidIdx + TIn::spatial_dimensions + k;

                if( block(j, k) ) dJ.insertBack(row, col) += (InReal)block[j][k];
            }
        }
    }

    dJ.finalize();
}


template <class TIn, class TOut>
const sofa::linearalgebra::BaseMatrix* RigidMapping<TIn, TOut>::getK()
{
    if( geometricStiffnessMatrix.compressedMatrix.nonZeros() ) return &geometricStiffnessMatrix;
    else return nullptr;
}


template <class TIn, class TOut>
const sofa::linearalgebra::BaseMatrix* RigidMapping<TIn, TOut>::getJ()
{
    const VecCoord& out =this->toModel->read(core::ConstVecCoordId::position())->getValue();
    const InVecCoord& in =this->fromModel->read(core::ConstVecCoordId::position())->getValue();
    const VecCoord& pts = this->getPoints();
    assert(pts.size() == out.size());

    if (matrixJ.get() == 0 || updateJ)
    {
        updateJ = false;
        if (matrixJ.get() == 0 ||
                (unsigned int)matrixJ->rowBSize() != out.size() ||
                (unsigned int)matrixJ->colBSize() != in.size())
        {
            matrixJ.reset(new MatrixType(out.size() * NOut, in.size() * NIn));
        }
        else
        {
            matrixJ->clear();
        }


        for (unsigned int outIdx = 0; outIdx < pts.size() ; outIdx++)
        {
            unsigned int inIdx = getRigidIndex(outIdx);

            setJMatrixBlock(outIdx, inIdx);
        }
    }
    matrixJ->compress();
    return matrixJ.get();
}

template<class Real>
struct RigidMappingMatrixHelper<2, Real>
{
    template <class Matrix, class Vector>
    static void setMatrix(Matrix& mat,
                          const Vector& vec)
    {
        mat[0][0] = (Real) 1     ;    mat[1][0] = (Real) 0     ;
        mat[0][1] = (Real) 0     ;    mat[1][1] = (Real) 1     ;
        mat[0][2] = (Real)-vec[1];    mat[1][2] = (Real) vec[0];
    }
};

template<class Real>
struct RigidMappingMatrixHelper<3, Real>
{
    template <class Matrix, class Vector>
    static void setMatrix(Matrix& mat,
                          const Vector& vec)
    {
        // out = J in
        // J = [ I -OM^ ]
        mat[0][0] = (Real) 1     ;    mat[1][0] = (Real) 0     ;    mat[2][0] = (Real) 0     ;
        mat[0][1] = (Real) 0     ;    mat[1][1] = (Real) 1     ;    mat[2][1] = (Real) 0     ;
        mat[0][2] = (Real) 0     ;    mat[1][2] = (Real) 0     ;    mat[2][2] = (Real) 1     ;
        mat[0][3] = (Real) 0     ;    mat[1][3] = (Real)-vec[2];    mat[2][3] = (Real) vec[1];
        mat[0][4] = (Real) vec[2];    mat[1][4] = (Real) 0     ;    mat[2][4] = (Real)-vec[0];
        mat[0][5] = (Real)-vec[1];    mat[1][5] = (Real) vec[0];    mat[2][5] = (Real) 0     ;
    }
};

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::setJMatrixBlock(unsigned outIdx, unsigned inIdx)
{
    MBloc& block = *matrixJ->wbloc(outIdx, inIdx, true);
    RigidMappingMatrixHelper<N, Real>::setMatrix(block, rotatedPoints[outIdx]);
}

template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::draw(const core::visual::VisualParams* vparams)
{
    if (!vparams->displayFlags().getShowMappings() || this->toModel==nullptr )
        return;
    std::vector<type::Vec3> points;
    type::Vec3 point;

    const VecCoord& x =this->toModel->read(core::ConstVecCoordId::position())->getValue();
    for (unsigned int i = 0; i < x.size(); i++)
    {
        point = OutDataTypes::getCPos(x[i]);
        points.push_back(point);
    }
    vparams->drawTool()->drawPoints(points, 7, sofa::type::RGBAColor::yellow() );
}


template <class TIn, class TOut>
void RigidMapping<TIn, TOut>::parse(core::objectmodel::BaseObjectDescription* arg)
{
    Inherit::parse(arg);

    // to be backward compatible with previous data structure
    const char* repartitionChar = arg->getAttribute("repartition");
    if( repartitionChar )
    {
        msg_deprecated() << "parse: You are using a deprecated Data 'repartition', please use the new structure data rigidIndexPerPoint";

        type::vector< unsigned int > repartition;
        std::istringstream ss( repartitionChar );
        repartition.read( ss );
        setRepartition( repartition );
    }
}

} // namespace sofa::component::mapping::nonlinear

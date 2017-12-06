//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CLogicalSequenceProject.h
//
//	@doc:
//		Logical Sequence Project operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CLogicalSequenceProject_H
#define GPOS_CLogicalSequenceProject_H

#include "gpos/base.h"

#include "gpopt/base/COrderSpec.h"
#include "gpopt/base/CWindowFrame.h"
#include "gpopt/operators/CLogicalUnary.h"

namespace gpopt
{
	// fwd declarations
	class CDistributionSpec;

	//---------------------------------------------------------------------------
	//	@class:
	//		CLogicalSequenceProject
	//
	//	@doc:
	//		Logical Sequence Project operator
	//
	//---------------------------------------------------------------------------
	class CLogicalSequenceProject : public CLogicalUnary
	{
		private:

			// partition by keys
			CDistributionSpec *m_pds;

			// order specs of child window functions
			DrgPos *m_pdrgpos;

			// frames of child window functions
			DrgPwf *m_pdrgpwf;

			// flag indicating if current operator has any non-empty order specs
			BOOL m_fHasOrderSpecs;

			// flag indicating if current operator has any non-empty frame specs
			BOOL m_fHasFrameSpecs;

			// set the flag indicating that SeqPrj has specified order specs
			void SetHasOrderSpecs(IMemoryPool *pmp);

			// set the flag indicating that SeqPrj has specified frame specs
			void SetHasFrameSpecs(IMemoryPool *pmp);

			// private copy ctor
			CLogicalSequenceProject(const CLogicalSequenceProject &);

		public:

			// ctor
			CLogicalSequenceProject
				(
				IMemoryPool *pmp,
				CDistributionSpec *pds,
				DrgPos *pdrgpos,
				DrgPwf *pdrgpwf
				);

			// ctor for pattern
			explicit
			CLogicalSequenceProject(IMemoryPool *pmp);

			// dtor
			virtual
			~CLogicalSequenceProject();

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopLogicalSequenceProject;
			}

			// operator name
			virtual
			const CHAR *SzId() const
			{
				return "CLogicalSequenceProject";
			}

			// distribution spec
			CDistributionSpec *Pds() const
			{
				return m_pds;
			}

			// order by keys
			DrgPos *Pdrgpos() const
			{
				return m_pdrgpos;
			}

			// frame specifications
			DrgPwf *Pdrgpwf() const
			{
				return m_pdrgpwf;
			}

			// return true if non-empty order specs are used by current operator
			BOOL FHasOrderSpecs() const
			{
				return m_fHasOrderSpecs;
			}

			// return true if non-empty frame specs are used by current operator
			BOOL FHasFrameSpecs() const
			{
				return m_fHasFrameSpecs;
			}

			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns(IMemoryPool *pmp, HMUlCr *phmulcr, BOOL fMustExist);

			// return true if we can pull projections up past this operator from its given child
			virtual
			BOOL FCanPullProjectionsUp
				(
				ULONG //ulChildIndex
				) const
			{
				return false;
			}

			//-------------------------------------------------------------------------------------
			// Derived Relational Properties
			//-------------------------------------------------------------------------------------

			// derive output columns
			virtual
			CColRefSet *PcrsDeriveOutput(IMemoryPool *pmp, CExpressionHandle &exprhdl);

			// derive outer references
			virtual
			CColRefSet *PcrsDeriveOuter(IMemoryPool *pmp, CExpressionHandle &exprhdl);

			// dervive keys
			virtual
			CKeyCollection *PkcDeriveKeys(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive max card
			virtual
			CMaxCard Maxcard(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive constraint property
			virtual
			CPropConstraint *PpcDeriveConstraint
				(
				IMemoryPool *, //pmp,
				CExpressionHandle &exprhdl
				)
				const
			{
				return PpcDeriveConstraintPassThru(exprhdl, 0 /*ulChild*/);
			}

			//-------------------------------------------------------------------------------------
			// Transformations
			//-------------------------------------------------------------------------------------

			// candidate set of xforms
			virtual
			CXformSet *PxfsCandidates(IMemoryPool *pmp) const;

			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------

			// derive statistics
			virtual
			IStatistics *PstatsDerive
						(
						IMemoryPool *pmp,
						CExpressionHandle &exprhdl,
						DrgPstat *pdrgpstatCtxt
						)
						const;

			// match function
			virtual
			BOOL FMatch(COperator *pop) const;

			virtual
			ULONG UlHash() const;

			// print
			virtual
			IOstream &OsPrint(IOstream &os) const;

			// remove outer references from Order By/ Partition By clauses, and return a new operator
			CLogicalSequenceProject *PopRemoveLocalOuterRefs(IMemoryPool *pmp, CExpressionHandle &exprhdl);

			// return true if outer references are included in Partition/Order, or window frame edges
			BOOL FHasLocalOuterRefs(CExpressionHandle &exprhdl) const;

			// conversion function
			static
			CLogicalSequenceProject *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopLogicalSequenceProject == pop->Eopid());

				return dynamic_cast<CLogicalSequenceProject*>(pop);
			}

	}; // class CLogicalSequenceProject

}

#endif // !GPOS_CLogicalSequenceProject_H

// EOF

//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		ITaskScheduler.h
//
//	@doc:
//		Interface class for task scheduling
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_ITaskScheduler_H
#define GPOS_ITaskScheduler_H

#include "gpos/types.h"

namespace gpos
{

	// prototypes
	class CTask;
	class CTaskId;

	//---------------------------------------------------------------------------
	//	@class:
	//		ITaskScheduler
	//
	//	@doc:
	//		Interface for abstracting task scheduling primitives.
	//
	//---------------------------------------------------------------------------

	class ITaskScheduler
	{
		private:

			// private copy ctor
			ITaskScheduler(const ITaskScheduler&);

		public:

			// dummy ctor
			ITaskScheduler() {}

			// dummy dtor
			virtual
			~ITaskScheduler() {}

			// add task to waiting queue
			virtual
			void Enqueue(CTask *) = 0;

			// get next task to execute
			virtual
			CTask *PtskDequeue() = 0;

			// check if task is waiting to be scheduled and remove it
			virtual
			GPOS_RESULT EresCancel(CTask *ptsk) = 0;

			// get number of waiting tasks
			virtual
			ULONG UlQueueSize() = 0;

			// check if task queue is empty
			virtual
			BOOL FEmpty() const = 0;

	};	// class ITaskScheduler
}

#endif /* GPOS_ITaskScheduler_H */

// EOF


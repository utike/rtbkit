/* parameters.h                                                    -*- C++ -*-
   Jeremy Barnes, 2 November 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Desciption of parameters.  Used to allow polymorphic updates of
   parameters.
*/

#ifndef __neural__parameters_h__
#define __neural__parameters_h__

#include <vector>
#include <boost/multi_array.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include "utils/hash_map.h"
#include "boosting/thread_context.h"
#include "db/persistent_fwd.h"
#include "stats/distribution.h"


namespace ML {

class Layer;



/*****************************************************************************/
/* LOCKING_POLICY                                                            */
/*****************************************************************************/

/** Describes how the locking is performed on the object when multiple threads
    can update.  They have different tradeoffs for thread occupancy versus
    efficiency.

    In a single threaded context, no locking is needed.
*/
enum Locking_Policy {
    LP_NONE,    ///< No locking (single threaded)
    LP_ATOMIC,  ///< Use atomic instructions
    LP_COARSE,  ///< Use one (coarse grained) lock
    LP_FINE     ///< Use fine grained locking per row (spinlock)
};


/*****************************************************************************/
/* PARAMETER_VALUE                                                           */
/*****************************************************************************/

struct Parameter_Value : boost::noncopyable {
    Parameter_Value(const std::string & name)
        : name_(name)
    {
    }

    virtual ~Parameter_Value() {}

    virtual size_t parameter_count() const = 0;

    virtual float * copy_to(float * where, float * limit) const = 0;
    virtual double * copy_to(double * where, double * limit) const = 0;
    
    /** Create a compatible parameters object, that refers to the data range
        given, not the current range.  The given range is not modified.  */
    virtual Parameter_Value *
    compatible_ref(float * first, float * last) const = 0;
    virtual Parameter_Value *
    compatible_ref(double * first, double * last) const = 0;

    /** Create a compatible parameters object, that refers to the data range
        given, not the current range.  The given range is initialized with
        the current values via copy_to.  */
    virtual Parameter_Value *
    compatible_copy(float * first, float * last) const;
    virtual Parameter_Value *
    compatible_copy(double * first, double * last) const;

    std::string name() const { return name_; }

protected:
    std::string name_;
};

template<typename Underlying>
struct Vector_Ref : public Parameter_Value {

    Vector_Ref(const std::string & name,
               const Underlying * array, size_t size)
        : Parameter_Value(name), array_(array), size_(size)
    {
    }

    virtual size_t parameter_count() const
    {
        return size_;
    }

    virtual float * copy_to(float * where, float * limit) const
    {
        if (limit - where > size_)
            throw Exception("copy_to(): wrong size array");
        std::copy(array_, array_ + size_, where);
        return where + size_;
    }

    virtual double * copy_to(double * where, double * limit) const
    {
        if (limit - where > size_)
            throw Exception("copy_to(): wrong size array");
        std::copy(array_, array_ + size_, where);
        return where + size_;
    }

    virtual Parameter_Value *
    compatible_ref(float * first, float * last) const
    {
        if (first + size_ != last)
            throw Exception("Vector_Ref::compatible_ref(): wrong size");
        return new Vector_Ref<float>(name(), first, size_);
    }

    virtual Parameter_Value *
    compatible_ref(double * first, double * last) const
    {
        if (first + size_ != last)
            throw Exception("Vector_Ref::compatible_ref(): wrong size");
        return new Vector_Ref<double>(name(), first, size_);
    }
    
protected:
    const Underlying * array_;
    size_t size_;
};

template<typename Underlying>
struct Matrix_Ref : public Parameter_Value {

    Matrix_Ref(const std::string & name,
               const Underlying * array, size_t size1, size_t size2)
        : Parameter_Value(name),
          array_(array), size1_(size1), size2_(size2)
    {
    }

    virtual size_t parameter_count() const
    {
        return size1_ * size2_;
    }

    virtual float * copy_to(float * where, float * limit) const
    {
        size_t n = parameter_count();
        if (limit - where > n)
            throw Exception("copy_to(): wrong size matrix");
        std::copy(array_, array_ + n, where);
        return where + n;
    }

    virtual double * copy_to(double * where, double * limit) const
    {
        size_t n = parameter_count();
        if (limit - where > n)
            throw Exception("copy_to(): wrong size matrix");
        std::copy(array_, array_ + n, where);
        return where + n;
    }

    virtual Parameter_Value *
    compatible_ref(float * first, float * last) const
    {
        size_t n = parameter_count();
        if (first + n != last)
            throw Exception("Matrix_Ref::compatible_ref(): wrong size");
        return new Matrix_Ref<float>(name(), first, size1_, size2_);
    }

    virtual Parameter_Value *
    compatible_ref(double * first, double * last) const
    {
        size_t n = parameter_count();
        if (first + n != last)
            throw Exception("Matrix_Ref::compatible_ref(): wrong size");
        return new Matrix_Ref<double>(name(), first, size1_, size2_);
    }
    
protected:
    const Underlying * array_;
    size_t size1_, size2_;
};


/*****************************************************************************/
/* PARAMETERS                                                                */
/*****************************************************************************/

struct Parameters : public Parameter_Value {

    /** Add a vector of values to the parameters */

    Parameters & add(int index, Parameter_Value * param);

    template<class Float>
    Parameters &
    add(int index,
        const std::string & name,
        std::vector<Float> & values)
    {
        return add(index,
                   new Vector_Ref<Float>(name, &values[0], values.size()));
    }
    
    /** Add a matrix of values to the parameters */
    template<class Float>
    Parameters &
    add(int index,
        const std::string & name,
        boost::multi_array<Float, 2> & values)
    {
        return add(index,
                   new Matrix_Ref<Float>(name, values.data(),
                                         values.shape()[0], values.shape()[1]));
    }

    size_t parameter_count() const;

    void serialize(DB::Store_Writer & store) const;

    /** Reconstitutes the object, not the parameters.  To reconstitute the
        parameters, first reconstitute a new object and then assign the
        new version. */
    void reconstitute(DB::Store_Reader & store);

    void fill(float value);

    void random_fill(float limit, Thread_Context & context);
    
    void operator -= (const Parameters & other);

    void operator += (const Parameters & other);

    double two_norm() const;

    void operator *= (double value);

    virtual void update(const Parameters & other, double learning_rate);

    virtual Parameters & subparams(int index, const std::string & name);
    virtual const Parameters &
    subparams(int index, const std::string & name) const;

    virtual void add_subparams(int index, Layer & layer);

    /** Concrete copy_to implementations */
    virtual float * copy_to(float * where, float * limit) const;
    virtual double * copy_to(double * where, double * limit) const;

    /** Create a compatible parameters object, that refers to the data range
        given, not the current range.  The given range is not modified.  */
    virtual Parameters *
    compatible_ref(float * first, float * last) const;
    virtual Parameters *
    compatible_ref(double * first, double * last) const;

    /** Create a compatible parameters object, that refers to the data range
        given, not the current range.  The given range is initialized with
        the current values via copy_to.  */
    virtual Parameters *
    compatible_copy(float * first, float * last) const;
    virtual Parameters *
    compatible_copy(double * first, double * last) const;

    /** Remove all parameter references from this object.  Doesn't actually
        modify any of the parameter values. */
    void clear();

#if 0
    template<typename FloatTo>
    FloatTo * copy_to(FloatTo * where, FloatTo * limit) const
    {
        // We have to implement in terms of float or double copy_to

    }
#endif    

protected:
    Parameters(const std::string & name);
    Parameters(const Parameters & other);

    Parameters & operator = (const Parameters & other);

    void swap(Parameters & other) const;

    std::hash_map<std::string, int> by_name;
    typedef boost::ptr_vector<Parameter_Value> Params;
    Params params;

    template<class Float> friend class Parameters_Copy;
};


/*****************************************************************************/
/* PARAMETERS_REF                                                            */
/*****************************************************************************/

/** Parameters that are stored somewhere else but referenced here. */

struct Parameters_Ref : public Parameters {
    virtual Parameters_Ref & subparams(int index, const std::string & name);
    virtual const Parameters_Ref &
    subparams(int index, const std::string & name) const;
};


/*****************************************************************************/
/* PARAMETERS_COPY                                                           */
/*****************************************************************************/

/** Storage of a value for each parameter, in the given type. */

template<class Float>
struct Parameters_Copy : public Parameters {
    Parameters_Copy();
    
    Parameters_Copy(const Parameters & other);

    Parameters_Copy(const Parameters_Copy & other);

    Parameters_Copy & operator = (const Parameters_Copy & other);

    Parameters_Copy(const Layer & layer);

    void swap(Parameters_Copy & other);

    /** Concrete copy_to implementations */
    virtual float * copy_to(float * where, float * limit) const;
    virtual double * copy_to(double * where, double * limit) const;

protected:
    // The actual values, stored contiguously for efficiency.
    distribution<Float> values;
};


extern template class Parameters_Copy<float>;
extern template class Parameters_Copy<double>;


} // namespace ML

#endif /* __neural__parameters_h__ */

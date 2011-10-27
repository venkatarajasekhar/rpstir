/* ***** BEGIN LICENSE BLOCK *****
 * 
 * BBN Rule Editor/Engine for Address and AS Number PKI
 * Verison 1.0
 * 
 * COMMERCIAL COMPUTER SOFTWARE�RESTRICTED RIGHTS (JUNE 1987)
 * US government users are permitted restricted rights as
 * defined in the FAR.  
 *
 * This software is distributed on an "AS IS" basis, WITHOUT
 * WARRANTY OF ANY KIND, either express or implied.
 *
 * Copyright (C) Raytheon BBN Technologies Corp. 2007.  All Rights Reserved.
 *
 * Contributor(s):  Charlie Gardiner
 *
 * ***** END LICENSE BLOCK ***** */
package orname;
import name.*;
import asn.*;
public class OrganizationalUnitName extends AsnStringArray
    {
    public OrganizationalUnitName()
        {
        _min = 1;
        _max = 32;
        }
    public OrganizationalUnitName set(OrganizationalUnitName frobj)
        {
        ((AsnObj)this).set(frobj);
	return this;
	}
    }
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DN_PEOPLE = 'ou=people,%s' % DEFAULT_SUFFIX
DN_ROOT = ''
TEST_USER_NAME = 'all_attrs_test'
TEST_USER_DN = 'uid=%s,%s' % (TEST_USER_NAME, DN_PEOPLE)
TEST_USER_PWD = 'all_attrs_test'

# Suffix for search, Regular user boolean, List of expected attrs
TEST_PARAMS = [(DN_ROOT, False, [
                    'aci', 'createTimestamp', 'creatorsName',
                    'modifiersName', 'modifyTimestamp', 'namingContexts',
                    'nsBackendSuffix', 'nsUniqueId', 'subschemaSubentry',
                    'supportedControl', 'supportedExtension',
                    'supportedFeatures', 'supportedLDAPVersion',
                    'supportedSASLMechanisms', 'vendorName', 'vendorVersion'
                ]),
               (DN_ROOT, True, [
                    'createTimestamp', 'creatorsName',
                    'modifiersName', 'modifyTimestamp', 'namingContexts',
                    'nsBackendSuffix', 'nsUniqueId', 'subschemaSubentry',
                    'supportedControl', 'supportedExtension',
                    'supportedFeatures', 'supportedLDAPVersion',
                    'supportedSASLMechanisms', 'vendorName', 'vendorVersion'
                ]),
               (DN_PEOPLE, False, [
                   'aci', 'createTimestamp', 'creatorsName', 'entrydn',
                   'entryid', 'modifiersName', 'modifyTimestamp',
                   'nsUniqueId', 'numSubordinates', 'parentid'
                ]),
               (DN_PEOPLE, True, [
                   'aci', 'createTimestamp', 'creatorsName', 'entrydn',
                   'entryid', 'modifyTimestamp', 'nsUniqueId',
                   'numSubordinates', 'parentid'
                ]),
               (TEST_USER_DN, False, [
                   'createTimestamp', 'creatorsName', 'entrydn',
                   'entryid', 'modifiersName', 'modifyTimestamp',
                   'nsUniqueId', 'parentid'
                ]),
               (TEST_USER_DN, True, [
                   'createTimestamp', 'creatorsName', 'entrydn',
                   'entryid', 'modifyTimestamp', 'nsUniqueId', 'parentid'
                ]),
               (DN_CONFIG, False, ['numSubordinates', 'passwordHistory'])]


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


@pytest.fixture(scope="module")
def test_user(topology):
    """User for binding operation"""

    try:
        topology.standalone.add_s(Entry((TEST_USER_DN, {
                                         'objectclass': 'top person'.split(),
                                         'objectclass': 'organizationalPerson',
                                         'objectclass': 'inetorgperson',
                                         'cn': TEST_USER_NAME,
                                         'sn': TEST_USER_NAME,
                                         'userpassword': TEST_USER_PWD,
                                         'mail': '%s@redhat.com' % TEST_USER_NAME,
                                         'uid': TEST_USER_NAME
                                        })))
    except ldap.LDAPError as e:
        log.error('Failed to add user (%s): error (%s)' % (TEST_USER_DN,
                                                           e.message['desc']))
        raise e


@pytest.fixture(scope="module")
def user_aci(topology):
    """Deny modifiersName attribute for the test user
    under whole suffix
    """

    ACI_TARGET = '(targetattr= "modifiersName")'
    ACI_ALLOW = '(version 3.0; acl "Deny modifiersName for user"; deny (read)'
    ACI_SUBJECT = ' userdn = "ldap:///%s";)' % TEST_USER_DN
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD,
                                                   'aci',
                                                   ACI_BODY)])


def test_supported_features(topology):
    """Verify that OID 1.3.6.1.4.1.4203.1.5.1 is published
    in the supportedFeatures [RFC3674] attribute in the rootDSE.

    :Feature: Filter

    :Setup: Standalone instance

    :Steps: 1. Search for 'supportedFeatures' at rootDSE

    :Assert: Value 1.3.6.1.4.1.4203.1.5.1 is presented
    """

    entries = topology.standalone.search_s('', ldap.SCOPE_BASE,
                                           '(objectClass=*)',
                                           ['supportedFeatures'])
    supported_value = entries[0].data['supportedfeatures']

    assert supported_value == ['1.3.6.1.4.1.4203.1.5.1']


@pytest.mark.parametrize('add_attr', ['', '*', 'objectClass'])
@pytest.mark.parametrize('search_suffix,regular_user,oper_attr_list',
                         TEST_PARAMS)
def test_search_basic(topology, test_user, user_aci, add_attr,
                      search_suffix, regular_user, oper_attr_list):
    """Verify that you can get all expected operational attributes
    by a Search Request [RFC2251] with '+' (ASCII 43) filter.
    Please see: https://tools.ietf.org/html/rfc3673

    :Feature: Filter

    :Setup: Standalone instance, test user for binding,
            deny one attribute aci for that user

    :Steps: 1. Bind as regular user or Directory Manager
            2. Search with '+' filter and with additionaly
               'objectClass' and '*' attrs too

    :Assert: All expected values were returned, not more
    """

    if regular_user:
        topology.standalone.simple_bind_s(TEST_USER_DN, TEST_USER_PWD)
    else:
        topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    search_filter = ['+']
    if add_attr:
        search_filter.append(add_attr)
        expected_attrs = sorted(oper_attr_list + ['objectClass'])
    else:
        expected_attrs = sorted(oper_attr_list)

    entries = topology.standalone.search_s(search_suffix, ldap.SCOPE_BASE,
                                           '(objectclass=*)',
                                           search_filter)
    found_attrs = sorted(entries[0].data.keys())

    if add_attr == '*':
        # Check that found attrs contain both operational
        # and non-operational attributes
        assert all(attr in found_attrs
                   for attr in ['objectClass', expected_attrs[0]])
    else:
        assert cmp(found_attrs, expected_attrs) == 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

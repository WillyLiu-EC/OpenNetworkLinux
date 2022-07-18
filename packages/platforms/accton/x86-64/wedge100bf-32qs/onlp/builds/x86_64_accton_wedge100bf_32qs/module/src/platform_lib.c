/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2017 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 *
 *
 ***********************************************************/
#include "platform_lib.h"

int bmc_curl_init(void)
{
    int i;

    for(i = 0; i<HANDLECOUNT; i++)
    {
        curl[i] = curl_easy_init();
        if (curl[i])
        {
            curl_easy_setopt(curl[i], CURLOPT_USERPWD, "root:0penBmc");
            curl_easy_setopt(curl[i], CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl[i], CURLOPT_SSL_VERIFYHOST, 0L);

        }
        else
        {
            AIM_LOG_ERROR("Unable to init curl[%d]\r\n", i);
            return -1;
        }
    }
    /* init a multi stack */
    multi_curl = curl_multi_init();

    return 0;
}

int bmc_curl_deinit(void)
{
    int i;

    printf("bmc_curl_deinit Start \n");
    for(i = 0; i<HANDLECOUNT; i++)
    {
        curl_multi_remove_handle(multi_curl, curl[i]);
        curl_easy_cleanup(curl[i]);
    }

    curl_multi_cleanup(multi_curl);

    return 0;
}

